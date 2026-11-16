// Tabla de estados ya vistos: la "lista cerrada" de la búsqueda.
//
// Equivale a `closed_t` del repositorio ksearch (clinaresl/ksearch), con dos
// diferencias que impone nuestro dominio:
//  - ksearch copia el nodo entero en el vector de la lista cerrada y otra vez
//    como clave del `unordered_multimap`; nuestros nodos ocupan 24 + 4·n bytes y
//    viven en la `Arena`, así que aquí solo se guarda su índice (4 B) y la
//    huella de 64 bits. La representación canónica del estado no se copia: se lee
//    de la arena cuando hace falta comparar;
//  - no hay `unordered_multimap` ni un `vector` de índices estables: es una tabla
//    de direccionamiento abierto con sondeo lineal, que evita una reserva de heap
//    por estado y mantiene las entradas contiguas.
//
// La clave es el par (nivel, estado físico). El nivel es imprescindible porque
// dos plazas vacías dejan el estado idéntico en niveles distintos y esos dos
// nodos **no** son comparables: lo que queda por decidir no es lo mismo. Sin el
// nivel se podarían soluciones válidas.
//
// Se consulta **al generar cada hijo** (no al extraerlo): un hijo que no mejora a
// nadie se descarta antes de entrar en la lista abierta y, sobre todo, su estado
// ya no se ramifica. El nodo se crea igualmente en la arena (el generador no
// consulta la tabla para no ensuciar su bucle), pero el subárbol que cuelga de él
// no se explora nunca.
//
// Qué se considera duplicado en nuestro problema: dos caminos que llegan al mismo
// reparto (mismo nivel, misma sede y mismo minuto de liberación por árbitro). Con
// el objetivo actual (solo se cobra el tiempo), asignar a un árbitro el principal
// o el asistente de un partido deja exactamente el mismo estado y el mismo coste,
// así que estos duplicados son habituales: cada partido con varios roles en la
// misma sede y hora los produce.
//
// Garantías:
//  - **Nunca se poda por una colisión de hash**: la huella solo selecciona el
//    cubo; la igualdad se decide comparando nivel y los 4·n bytes del estado.
//  - Al reemplazar un estado por otro nodo con menor `coste_g`, el nodo antiguo
//    se marca con `kFlagSuperado`. Sigue en la arena y puede seguir en la lista
//    abierta, pero la búsqueda lo descarta al extraerlo.
//  - Si el candidato no mejora (mismo coste o peor), `mejorar` devuelve `false` y
//    el nodo nuevo se queda sin marcar: nunca entra en la lista abierta, así que
//    nadie lo va a extraer. El primer nodo llegado gana los empates, lo que hace
//    la búsqueda reproducible.
//
// Es una estructura de datos, no un algoritmo: vive en cabecera, como `Arena`,
// para que el sondeo se inline en el bucle caliente.

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "referee_scheduler/algorithms/arena.hpp"

namespace referee_scheduler::algorithms
{

    /// Capacidad inicial de la tabla (potencia de dos).
    inline constexpr std::size_t kCapacidadInicialTabla = 64;

    /// Huella de 64 bits de la clave (nivel, estado).
    ///
    /// Se recorre el estado en palabras de 32 bits (cada `EstadoArbitro` es
    /// exactamente 32 bits) con una FNV-1a y se mezcla el nivel con la constante
    /// áurea. El mezclador final de 64 bits (splitmix64) es necesario porque la
    /// tabla indexa con una máscara de bits bajos: sin él, los estados que solo
    /// cambian en los bits altos de una palabra se agruparían en los mismos cubos.
    ///
    /// La huella **no** decide por sí sola: `TablaEstados` compara siempre el
    /// nivel y los bytes del estado antes de dar dos nodos por iguales.
    inline std::uint64_t huella_estado(std::size_t nivel,
                                       std::span<const EstadoArbitro> estado) noexcept
    {
        constexpr std::uint64_t offset_fnv = 1469598103934665603ull;
        constexpr std::uint64_t primo_fnv = 1099511628211ull;
        constexpr std::uint64_t aurea = 0x9E3779B97F4A7C15ull;

        std::uint64_t huella =
            offset_fnv ^ (static_cast<std::uint64_t>(nivel) * aurea);

        const auto *bytes = reinterpret_cast<const std::byte *>(estado.data());
        const std::size_t n_bytes = estado.size() * sizeof(EstadoArbitro);

        std::size_t i = 0;
        for (; i + 4 <= n_bytes; i += 4)
        {
            std::uint32_t palabra = 0;
            std::memcpy(&palabra, bytes + i, sizeof(palabra));
            huella = (huella ^ palabra) * primo_fnv;
        }
        // `EstadoArbitro` mide 4 B, así que este bucle no se ejecuta nunca; queda
        // por si algún día el registro cambia de tamaño.
        for (; i < n_bytes; ++i)
        {
            huella = (huella ^ static_cast<std::uint64_t>(bytes[i])) * primo_fnv;
        }

        // Mezcla final (splitmix64): reparte la entropía hacia los bits bajos.
        huella ^= huella >> 30;
        huella *= 0xBF58476D1CE4E5B9ull;
        huella ^= huella >> 27;
        huella *= 0x94D049BB133111EBull;
        huella ^= huella >> 31;
        return huella;
    }

    /// Una entrada de la tabla: la huella y el nodo canónico del estado.
    ///
    /// `nodo == kSinNodo` marca el hueco libre. La huella se guarda para poder
    /// rehacer la tabla (al crecer) sin leer la arena: el nuevo índice sale de la
    /// huella, que ya está aquí.
    struct EntradaTabla
    {
        std::uint64_t huella = 0;
        std::uint32_t nodo = kSinNodo;
    };

    // 8 + 4 redondeado a la alineación de 8: 16 B por entrada. A factor de carga
    // 0,5 son 32 B por estado distinto, frente a los 504 B de una copia del nodo.
    static_assert(sizeof(EntradaTabla) == 16,
                  "la entrada de la tabla debe ocupar 16 B");

    /// Tabla de estados vistos: mapea (nivel, estado) al nodo que mejor lo
    /// representa.
    class TablaEstados
    {
    public:
        /// Crea una tabla vacía con capacidad para `capacidad_inicial` entradas
        /// (se redondea a la potencia de dos siguiente).
        explicit TablaEstados(std::size_t capacidad_inicial = kCapacidadInicialTabla)
            : entradas_(redondear_potencia_dos(capacidad_inicial)),
              mascara_(entradas_.size() - 1)
        {
        }

        TablaEstados(const TablaEstados &) = default;
        TablaEstados &operator=(const TablaEstados &) = default;
        TablaEstados(TablaEstados &&) noexcept = default;
        TablaEstados &operator=(TablaEstados &&) noexcept = default;

        /// Da de alta `nodo` como candidato a representar su estado.
        ///
        /// Si el estado (nivel + 4·n bytes) no estaba, ocupa un hueco y devuelve
        /// `true`. Si ya estaba, solo lo sustituye cuando `coste_g` es
        /// **estrictamente menor**; en ese caso marca el nodo antiguo con
        /// `kFlagSuperado`, y también devuelve `true`. Si el candidato empata o
        /// empeora, devuelve `false` y no toca nada (salvo la propia tabla).
        ///
        /// @param arena Arena que contiene el nodo (se lee su cabecera y su
        ///              estado, y se escribe el bit de superado del nodo viejo).
        /// @param nodo Nodo recién creado por el generador de hijos.
        /// @return `true` si el nodo debe entrar en la lista abierta.
        /// @pre `nodo` existe en la arena y `arena.n_arbitros() > 0`.
        bool mejorar(Arena &arena, std::uint32_t nodo)
        {
            assert(nodo < arena.nodos_creados() && "nodo fuera de la arena");
            assert(arena.n_arbitros() > 0);

            const std::span<const EstadoArbitro> estado = arena.estado(nodo);
            const CabeceraNodo &cabecera = arena.cabecera(nodo);
            const std::size_t nivel = cabecera.nivel;
            const std::uint64_t huella = huella_estado(nivel, estado);

            std::size_t indice = huella & mascara_;
            for (;;)
            {
                EntradaTabla &entrada = entradas_[indice];

                if (entrada.nodo == kSinNodo)
                {
                    // Estado nuevo: ocupa el hueco y, si la tabla se queda
                    // demasiado llena, crece. El crecimiento va después de
                    // escribir porque rehash invalida `entrada`.
                    entrada.huella = huella;
                    entrada.nodo = nodo;
                    ++ocupadas_;

                    if (ocupadas_ * 2 >= entradas_.size())
                    {
                        rehash(entradas_.size() * 2);
                    }
                    return true;
                }

                if (entrada.huella == huella &&
                    mismo_estado(arena, entrada.nodo, nodo))
                {
                    const std::int64_t coste_actual = arena.cabecera(entrada.nodo).coste_g;
                    if (cabecera.coste_g < coste_actual)
                    {
                        // Mejor camino para el mismo estado: el nodo antiguo
                        // deja de ser el representante canónico. Puede estar
                        // todavía en la lista abierta; al extraerlo, el bit de
                        // superado lo descarta.
                        arena.cabecera(entrada.nodo).flags |= kFlagSuperado;
                        entrada.nodo = nodo;
                        return true;
                    }
                    return false;
                }

                indice = (indice + 1) & mascara_;
            }
        }

        /// Nodo canónico del estado (nivel, estado), o `kSinNodo` si no se ha
        /// visto todavía.
        ///
        /// No se usa en el bucle de la búsqueda (allí basta el bit de superado);
        /// sirve para los tests, para depurar y para consultar desde fuera.
        std::uint32_t nodo(const Arena &arena,
                           std::size_t nivel,
                           std::span<const EstadoArbitro> estado) const noexcept
        {
            assert(estado.size() == arena.n_arbitros());

            const std::uint64_t huella = huella_estado(nivel, estado);
            std::size_t indice = huella & mascara_;
            for (;;)
            {
                const EntradaTabla &entrada = entradas_[indice];
                if (entrada.nodo == kSinNodo)
                {
                    return kSinNodo;
                }
                if (entrada.huella == huella &&
                    arena.cabecera(entrada.nodo).nivel == nivel &&
                    std::memcmp(arena.estado(entrada.nodo).data(), estado.data(),
                                estado.size() * sizeof(EstadoArbitro)) == 0)
                {
                    return entrada.nodo;
                }
                indice = (indice + 1) & mascara_;
            }
        }

        /// Igual que el anterior, pero tomando el estado del propio nodo.
        std::uint32_t nodo(const Arena &arena, std::uint32_t candidato) const noexcept
        {
            assert(candidato < arena.nodos_creados());
            return nodo(arena, arena.cabecera(candidato).nivel,
                        arena.estado(candidato));
        }

        /// Estados distintos registrados (no es el número de nodos creados: los
        /// duplicados y los superados no cuentan).
        std::size_t size() const noexcept { return ocupadas_; }

        /// Entradas reservadas (potencia de dos).
        std::size_t capacidad() const noexcept { return entradas_.size(); }

        /// Bytes comprometidos por la tabla.
        std::size_t memoria_reservada() const noexcept
        {
            return entradas_.capacity() * sizeof(EntradaTabla);
        }

        /// Reserva capacidad para al menos `estados` estados distintos.
        void reservar(std::size_t estados)
        {
            // Factor de carga 0,5: hacen falta el doble de huecos que estados.
            const std::size_t necesaria = redondear_potencia_dos(estados * 2);
            if (necesaria > entradas_.size())
            {
                rehash(necesaria);
            }
        }

    private:
        /// Potencia de dos mayor o igual que `n` (mínimo 8).
        static std::size_t redondear_potencia_dos(std::size_t n) noexcept
        {
            std::size_t potencia = 8;
            while (potencia < n)
            {
                potencia *= 2;
            }
            return potencia;
        }

        /// ¿Los dos nodos representan el mismo estado en el mismo nivel?
        ///
        /// La comparación es exacta (nivel + `memcmp` de los 4·n bytes), no una
        /// comparación de huellas: así una colisión nunca puede podar una
        /// solución válida.
        static bool mismo_estado(const Arena &arena,
                                 std::uint32_t nodo_a,
                                 std::uint32_t nodo_b) noexcept
        {
            const CabeceraNodo &cab_a = arena.cabecera(nodo_a);
            const CabeceraNodo &cab_b = arena.cabecera(nodo_b);
            if (cab_a.nivel != cab_b.nivel)
            {
                return false;
            }

            const std::span<const EstadoArbitro> estado_a = arena.estado(nodo_a);
            const std::span<const EstadoArbitro> estado_b = arena.estado(nodo_b);
            return std::memcmp(estado_a.data(), estado_b.data(),
                               estado_a.size() * sizeof(EstadoArbitro)) == 0;
        }

        /// Rehace la tabla con `capacidad` entradas (potencia de dos).
        ///
        /// Usa solo la huella ya almacenada, así que no lee la arena y el coste
        /// es proporcional a la capacidad, no a `n_arbitros`.
        void rehash(std::size_t capacidad)
        {
            std::vector<EntradaTabla> viejas(redondear_potencia_dos(capacidad));
            viejas.swap(entradas_);
            mascara_ = entradas_.size() - 1;
            ocupadas_ = 0;

            for (const EntradaTabla &entrada : viejas)
            {
                if (entrada.nodo != kSinNodo)
                {
                    insertar_huella(entrada.huella, entrada.nodo);
                }
            }
        }

        /// Inserta una entrada ya calculada en el primer hueco libre.
        void insertar_huella(std::uint64_t huella, std::uint32_t nodo) noexcept
        {
            std::size_t indice = huella & mascara_;
            while (entradas_[indice].nodo != kSinNodo)
            {
                indice = (indice + 1) & mascara_;
            }
            entradas_[indice].huella = huella;
            entradas_[indice].nodo = nodo;
            ++ocupadas_;
        }

        /// Entradas de la tabla (tamaño potencia de dos; `kSinNodo` = libre).
        std::vector<EntradaTabla> entradas_;

        /// Máscara para reducir un índice a `[0, capacidad)`.
        std::size_t mascara_ = 0;

        /// Entradas ocupadas.
        std::size_t ocupadas_ = 0;
    };

} // namespace referee_scheduler::algorithms
