// Almacén de nodos de búsqueda con direcciones estables: cada nodo es una
// cabecera de 24 B seguida de los `n_arbitros` registros de 4 B del día.
// El tamaño de bloque se calcula en tiempo de ejecución (`24 + 4·n`), así que
// un día con pocos árbitros no paga el peor caso, y los nodos viven en bloques
// de heap que nunca se mueven: crear uno es avanzar un contador y copiar su
// estado. No depende del cargador ni de `domain::Instancia`.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <vector>

#include "referee_scheduler/algorithms/estado.hpp"

namespace referee_scheduler::algorithms
{

    /// Índice que significa "no hay nodo". Se usa, por ejemplo, como `padre` de la raíz.
    inline constexpr std::uint32_t kSinNodo = std::numeric_limits<std::uint32_t>::max();

    /// Valor de `CabeceraNodo::decision` para una plaza que se deja vacía.
    ///
    /// Se elige el mayor valor de 32 bits para no confundirlo con ningún id
    /// compacto de árbitro (`0..kMaxArbitros-1`).
    inline constexpr std::uint32_t kDecisionVacia = 0xFFFFFFFFu;

    /// Valor de `CabeceraNodo::decision` en la raíz: no hay decisión previa.
    inline constexpr std::uint32_t kSinDecision = 0xFFFFFFFEu;

    // Los dos centinelas deben ser distintos entre sí y quedar por encima de
    // cualquier id compacto de árbitro; así `decision_es_asignacion` no tiene
    // que consultar la instancia del día para descartarlos.
    static_assert(kDecisionVacia != kSinDecision,
                  "los centinelas de decision deben ser distintos");
    static_assert(static_cast<std::uint64_t>(kMaxArbitros) < kSinDecision,
                  "los ids compactos de arbitro no deben alcanzar los centinelas");

    /// ¿La decisión asigna la plaza a un árbitro compacto?
    constexpr bool decision_es_asignacion(std::uint32_t decision) noexcept
    {
        return decision < static_cast<std::uint32_t>(kMaxArbitros);
    }

    /// ¿La decisión deja la plaza vacía?
    constexpr bool decision_es_vacia(std::uint32_t decision) noexcept
    {
        return decision == kDecisionVacia;
    }

    /// ¿Es la decisión de la raíz (sin decisión)?
    constexpr bool decision_es_raiz(std::uint32_t decision) noexcept
    {
        return decision == kSinDecision;
    }

    /// Bits de `CabeceraNodo::flags` reservados para la frontera del paso 4.
    ///
    /// El generador de hijos los deja a cero; la frontera decidirá cuándo marcar
    /// un nodo como abierto o cerrado.
    inline constexpr std::uint32_t kFlagAbierto = 1u << 0;
    inline constexpr std::uint32_t kFlagCerrado = 1u << 1;

    /// Cabecera de decisión que la arena guarda delante del estado de cada nodo.
    ///
    /// Son los 24 B de "metadatos" del bloque: no forman parte del estado físico
    /// del día, sino del nodo de búsqueda. El tamaño NO puede cambiar sin tocar
    /// la fórmula del bloque de la arena:
    ///
    ///     tam_bloque = sizeof(CabeceraNodo) + 4 * n_arbitros
    ///
    /// Reparto: 8 + 4 + 4 + 4 + 4 = 24 B.
    struct CabeceraNodo
    {
        /// Coste acumulado desde la raíz, en unidades de la función objetivo
        std::int64_t coste_g = 0;

        /// Nodo del que se generó este, o `kSinNodo` si es la raíz del árbol.
        std::uint32_t padre = kSinNodo;

        /// Nivel de decisión alcanzado: cuántas plazas se han decidido ya.
        /// Sirve para saber qué toca expandir y para reconstruir la solución.
        std::uint32_t nivel = 0;

        /// Marcas de la búsqueda (abierto/cerrado, etc.). Las define el paso de
        /// la frontera; aquí solo se reserva el hueco.
        std::uint32_t flags = 0;

        /// Decisión que llevó a este nodo desde su padre:
        ///  - `0 .. n_arbitros-1`: se asignó la plaza a ese árbitro compacto;
        ///  - `kDecisionVacia`: se dejó la plaza vacía;
        ///  - `kSinDecision`: nodo raíz, sin decisión.
        ///
        /// Guardarla aquí evita reconstruir la solución comparando estados.
        /// Para leerla, un nodo con `nivel = p + 1` decidió `plazas[p]`.
        std::uint32_t decision = kSinDecision;
    };

    static_assert(sizeof(CabeceraNodo) == 24,
                  "La cabecera del nodo debe ocupar exactamente 24 B: la arena "
                  "calcula el tamaño de bloque como 24 + 4 * n_arbitros");

    static_assert(std::is_trivially_copyable_v<CabeceraNodo>,
                  "La cabecera debe poder copiarse byte a byte");

    /// Almacén de nodos de búsqueda con direcciones estables.
    ///
    /// Cada nodo es un bloque contiguo de `tam_bloque()` bytes:
    ///
    ///     [ CabeceraNodo (24 B) ][ EstadoArbitro * n_arbitros (4 B cada uno) ]
    ///
    /// El tamaño se calcula **en tiempo de ejecución** a partir de `n_arbitros`,
    /// no de `kMaxArbitros`: si un día tiene 120 árbitros, cada nodo ocupa 504 B
    /// y no los 2424 B del peor caso. Por eso la arena recibe `n_arbitros` y no
    /// depende del cargador ni de una hipotética `Instancia` (paso 5).
    ///
    /// Relación con `Estado`: `Estado` es la vista de capacidad máxima (600
    /// árbitros = 2400 B) que se usa para construir el estado inicial o para
    /// depurar. La arena guarda solo las `n_arbitros` entradas significativas;
    /// el resto del array de `Estado`, si lo hay, se ignora en la copia.
    ///
    /// Garantías:
    ///  - **Direcciones estables**: los nodos viven en bloques de heap
    ///    independientes que nunca se mueven ni se copian. Crear nodos nuevos no
    ///    invalida los punteros ni los índices ya devueltos. Mover la `Arena`
    ///    tampoco los invalida (solo se mueve el vector de punteros a bloques).
    ///  - **Sin recorridos fuera de rango**: `estado(nodo)` devuelve un
    ///    `std::span` de longitud `n_arbitros()`, así que es imposible iterar por
    ///    accidente las ranuras de relleno.
    ///  - **Sin `new` por nodo**: los bloques se crean por lotes (y se duplican
    ///    al crecer), de modo que crear un nodo es solo avanzar un contador y
    ///    copiar `4 * n_arbitros` bytes.
    ///
    /// La arena no es copiable (duplicar millones de nodos no tiene sentido);
    /// sí es movible.
    class Arena
    {
    public:
        /// Tamaño objetivo de cada bloque de heap. 64 KiB amortiza el coste de
        /// `new` sin desperdiciar demasiado al final de la búsqueda.
        static constexpr std::size_t kBytesPorBloque = 64 * 1024;

        /// Crea una arena para estados de exactamente `n_arbitros` árbitros.
        ///
        /// `n_arbitros` es el número real del día (ya compactado a `0..n-1`), no
        /// la capacidad de compilación. Debe cumplir `1 <= n_arbitros <=
        /// kMaxArbitros`; en otro caso el fallo es de programación y se detecta
        /// con `assert` (en release la responsabilidad es del cargador).
        explicit Arena(std::size_t n_arbitros)
            : n_arbitros_(n_arbitros),
              tam_bloque_(calcular_tam_bloque(n_arbitros))
        {
            assert(n_arbitros > 0 && "un dia sin arbitros no genera busqueda");
            assert(n_arbitros <= kMaxArbitros &&
                   "n_arbitros no cabe en Estado: revisa la capacidad de compilacion");

            // Cuántos nodos caben en un bloque de heap. Con n = 600 cada nodo
            // ocupa 2424 B, así que entran 27 por bloque.
            nodos_por_bloque_ = kBytesPorBloque / tam_bloque_;
            if (nodos_por_bloque_ == 0)
            {
                // Un solo nodo ya llena el bloque: el bloque será mayor de 64 KiB.
                nodos_por_bloque_ = 1;
            }
        }

        Arena(const Arena &) = delete;
        Arena &operator=(const Arena &) = delete;
        Arena(Arena &&) noexcept = default;
        Arena &operator=(Arena &&) noexcept = default;

        /// Número de árbitros que representa cada estado de esta arena.
        std::size_t n_arbitros() const noexcept { return n_arbitros_; }

        /// Bytes que ocupa un nodo: cabecera + un `EstadoArbitro` por árbitro.
        std::size_t tam_bloque() const noexcept { return tam_bloque_; }

        /// Nodos creados hasta ahora (también es el índice del siguiente nodo).
        std::size_t nodos_creados() const noexcept { return creados_; }

        /// Nodos para los que ya hay memoria reservada.
        std::size_t capacidad() const noexcept
        {
            return nodos_por_bloque_ * bloques_.size();
        }

        /// Bytes de heap comprometidos por los bloques.
        std::size_t memoria_reservada() const noexcept
        {
            return capacidad() * tam_bloque_;
        }

        /// Tamaño exacto de un nodo para `n_arbitros`.
        ///
        /// La fórmula acordada es `24 + 4 * n_arbitros`; además se redondea hacia
        /// arriba a 8 B para que **todos** los nodos empiecen alineados a 8 (la
        /// cabecera contiene un `int64_t`). Con `n` par el redondeo no añade nada
        /// (600 -> 2424 B); con `n` impar añade 4 B como mucho.
        static constexpr std::size_t calcular_tam_bloque(std::size_t n_arbitros) noexcept
        {
            const std::size_t crudo =
                sizeof(CabeceraNodo) + n_arbitros * sizeof(EstadoArbitro);
            constexpr std::size_t alineacion = alignof(CabeceraNodo);
            return (crudo + alineacion - 1) / alineacion * alineacion;
        }

        /// Garantiza memoria para al menos `nodos` nodos.
        ///
        /// Nunca mueve los nodos existentes: solo añade bloques nuevos. El número
        /// de bloques crece duplicando, para que reservar sea amortizado O(1).
        void reservar(std::size_t nodos)
        {
            const std::size_t necesarios =
                (nodos + nodos_por_bloque_ - 1) / nodos_por_bloque_;
            if (necesarios <= bloques_.size())
            {
                return;
            }

            // Duplicar (nunca menos de lo necesario) mantiene el coste amortizado
            // constante aunque se llame a `reservar` en cada creación.
            const std::size_t objetivo =
                std::max(necesarios, bloques_.size() * 2);

            while (bloques_.size() < objetivo)
            {
                bloques_.push_back(
                    std::make_unique<std::byte[]>(nodos_por_bloque_ * tam_bloque_));
            }
        }

        /// Crea un nodo copiando las `n_arbitros` primeras entradas de `origen`.
        ///
        /// `origen` puede ser más largo que `n_arbitros` (por ejemplo, el array de
        /// 600 de un `Estado`); solo se copia la parte significativa y el resto se
        /// ignora. Devuelve el índice del nodo, que es estable para siempre.
        std::uint32_t crear(std::span<const EstadoArbitro> origen,
                            const CabeceraNodo &cabecera)
        {
            assert(origen.size() >= n_arbitros_ &&
                   "el estado de origen no tiene entradas para todos los arbitros");

            crear_si_hace_falta();
            const auto nodo = static_cast<std::uint32_t>(creados_);
            std::byte *memoria = direccion(nodo);

            // La memoria del bloque es cruda (`std::byte`), así que se coloca el
            // objeto con placement new: así existe formalmente y no dependemos de
            // reglas de creación implícita. Después se copian los 4 * n bytes del
            // estado; `EstadoArbitro` es trivially copyable, por lo que `memcpy`
            // es equivalente a asignar campo a campo.
            ::new (static_cast<void *>(memoria)) CabeceraNodo(cabecera);
            auto *destino = ::new (static_cast<void *>(
                memoria + sizeof(CabeceraNodo))) EstadoArbitro[n_arbitros_];
            std::memcpy(destino, origen.data(),
                        n_arbitros_ * sizeof(EstadoArbitro));

            ++creados_;
            return nodo;
        }

        /// Igual que el anterior, pero tomando el estado de un `Estado` completo.
        ///
        /// Es el puente natural con el estado inicial: se construye en un `Estado`
        /// (capacidad máxima) y la arena copia solo `[0, n_arbitros)`.
        std::uint32_t crear(const Estado &origen, const CabeceraNodo &cabecera)
        {
            return crear(std::span<const EstadoArbitro>(origen.arbitros.data(),
                                                        n_arbitros_),
                         cabecera);
        }

        /// Cabecera de decisión del nodo (24 B). Se puede modificar.
        CabeceraNodo &cabecera(std::uint32_t nodo) noexcept
        {
            assert(nodo < creados_ && "nodo fuera de la arena");
            // `launder` es necesario porque el objeto se creó con placement new
            // sobre memoria cruda y accedemos por una dirección recomputada.
            return *std::launder(reinterpret_cast<CabeceraNodo *>(direccion(nodo)));
        }

        /// Cabecera de decisión del nodo (solo lectura).
        const CabeceraNodo &cabecera(std::uint32_t nodo) const noexcept
        {
            assert(nodo < creados_ && "nodo fuera de la arena");
            return *std::launder(reinterpret_cast<const CabeceraNodo *>(direccion(nodo)));
        }

        /// Estado físico del nodo como span de `n_arbitros()` entradas.
        ///
        /// El span es la forma de garantizar que la búsqueda (Dijkstra incluido)
        /// recorre exactamente los árbitros del día y nunca el relleno.
        std::span<EstadoArbitro> estado(std::uint32_t nodo) noexcept
        {
            assert(nodo < creados_ && "nodo fuera de la arena");
            auto *datos = std::launder(reinterpret_cast<EstadoArbitro *>(
                direccion(nodo) + sizeof(CabeceraNodo)));
            return {datos, n_arbitros_};
        }

        /// Estado físico del nodo como span de solo lectura.
        std::span<const EstadoArbitro> estado(std::uint32_t nodo) const noexcept
        {
            assert(nodo < creados_ && "nodo fuera de la arena");
            auto *datos = std::launder(reinterpret_cast<const EstadoArbitro *>(
                direccion(nodo) + sizeof(CabeceraNodo)));
            return {datos, n_arbitros_};
        }

    private:
        /// Asegura que hay hueco para un nodo más.
        void crear_si_hace_falta()
        {
            assert(creados_ < kSinNodo && "se agotaron los indices de nodo");
            if (creados_ == capacidad())
            {
                reservar(creados_ + 1);
            }
        }

        /// Dirección del bloque del nodo `nodo`.
        std::byte *direccion(std::uint32_t nodo) noexcept
        {
            const std::size_t bloque = nodo / nodos_por_bloque_;
            const std::size_t dentro = nodo % nodos_por_bloque_;
            return bloques_[bloque].get() + dentro * tam_bloque_;
        }

        /// Dirección del bloque del nodo `nodo` (solo lectura).
        const std::byte *direccion(std::uint32_t nodo) const noexcept
        {
            const std::size_t bloque = nodo / nodos_por_bloque_;
            const std::size_t dentro = nodo % nodos_por_bloque_;
            return bloques_[bloque].get() + dentro * tam_bloque_;
        }

        /// Árbitros del día que representa cada estado.
        std::size_t n_arbitros_ = 0;

        /// Bytes por nodo, ya redondeados para alinear la cabecera.
        std::size_t tam_bloque_ = 0;

        /// Nodos que caben en un bloque de heap.
        std::size_t nodos_por_bloque_ = 0;

        /// Nodos creados; también el índice que recibirá el próximo.
        std::size_t creados_ = 0;

        /// Bloques de heap. `unique_ptr` mantiene las direcciones fijas: el
        /// vector puede reubicarse al crecer, pero los bloques no se mueven.
        std::vector<std::unique_ptr<std::byte[]>> bloques_;
    };

} // namespace referee_scheduler::algorithms
