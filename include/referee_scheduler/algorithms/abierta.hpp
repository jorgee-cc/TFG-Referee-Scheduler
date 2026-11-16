// Lista abierta de Dijkstra como vector de cubos ("buckets") indexados por la
// clave entera del nodo.
//
// La idea está tomada de `bucket_t` del repositorio ksearch (clinaresl/ksearch),
// pero adaptada a nuestro dominio:
//  - las claves son céntimos enteros de 64 bits (`std::int64_t`), no `int`, y
//    pueden estar lejos del cero: el cubo 0 corresponde a `base()`, la clave más
//    pequeña insertada hasta ahora;
//  - lo que viaja por la lista es solo el índice de nodo de la `Arena` (4 B), no
//    el nodo entero: el estado físico de 4·n_arbitros bytes se queda en la arena
//    y la clave se lee de `CabeceraNodo::coste_g`;
//  - no hay borrado arbitrario (`remove(idx)` en ksearch): Dijkstra no lo
//    necesita. Un nodo que deja de interesar se descarta al extraerlo con el bit
//    `kFlagSuperado`, no se busca dentro de su cubo.
//
// Por qué cubos y no un heap: en este problema las claves son céntimos enteros
// no negativos y, sobre todo, **no decrecientes** (cada arista suma al menos
// `kEpsilonCentimos = 1`, y las plazas vacías suman su retribución). Con claves
// enteras, insertar y extraer el mínimo son O(1) amortizado y sin comparadores:
// basta con avanzar un cursor sobre el vector de cubos.
//
// Invariantes:
//  - `base_` es la clave (alineada a `anchura_`) del cubo 0 y no cambia mientras
//    exista algún cubo: el llamador no puede insertar una clave que caiga en un
//    cubo ya consumido (Dijkstra lo garantiza; ver `insertar`).
//  - `primero_` es el índice del primer cubo no vacío; solo avanza.
//  - `total_` es el número de nodos encolados (suma de los tamaños de los cubos).
//  - Dentro de un cubo el orden es LIFO: se inserta al final y se extrae también
//    por el final. Con `anchura_ == 1` todos los nodos de un cubo tienen la misma
//    clave, así que el orden interno no afecta a la optimalidad, solo al orden de
//    exploración (LIFO bucea hacia el fondo y abarata la extracción).
//
// `anchura_` (por defecto 1 céntimo, es decir, orden exacto) permite agrupar
// varias claves en un mismo cubo para ahorrar memoria cuando el rango de claves
// se dispara. Con `anchura_ > 1` la extracción sigue siendo no decreciente pero
// deja de ser exacta, así que la búsqueda no puede parar en la primera meta: debe
// seguir mientras `clave_min() < incumbente` (ver `busqueda.hpp`). Nunca se pierde
// la optimalidad, solo se exploran más nodos.
//
// Es una estructura de datos, no un algoritmo: vive en cabecera, como `Arena`,
// para que el bucle caliente la inline. No es copiable (duplicar la frontera no
// tiene sentido) y sí movible.

#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace referee_scheduler::algorithms
{

    /// Lista abierta de Dijkstra: cubos de índices de nodo ordenados por clave.
    ///
    /// Uso típico (ver `buscar_dijkstra`):
    ///
    ///     ListaAbierta abierta;
    ///     abierta.insertar(raiz, 0);
    ///     while (!abierta.vacia() && abierta.clave_min() < mejor) {
    ///         const std::uint32_t nodo = abierta.extraer_min();
    ///         ...
    ///     }
    class ListaAbierta
    {
    public:
        /// Crea una lista abierta con cubos de `anchura` céntimos.
        ///
        /// `anchura == 1` (el valor por defecto) da orden exacto: un cubo por
        /// céntimo. Valores mayores agrupan claves y ahorran memoria a cambio de
        /// explorar más nodos antes de poder demostrar la optimalidad.
        explicit ListaAbierta(std::int64_t anchura = 1) noexcept
            : anchura_(anchura)
        {
            assert(anchura > 0 && "la anchura de un cubo debe ser positiva");
        }

        // Una frontera no se copia: duplicarla duplicaría el estado de la
        // búsqueda y no hay ningún caso de uso.
        ListaAbierta(const ListaAbierta &) = delete;
        ListaAbierta &operator=(const ListaAbierta &) = delete;
        ListaAbierta(ListaAbierta &&) noexcept = default;
        ListaAbierta &operator=(ListaAbierta &&) noexcept = default;

        /// Encola `nodo` en el cubo de `clave`.
        ///
        /// @param nodo Índice del nodo en la `Arena` (4 B); la lista no lo toca.
        /// @param clave Coste con el que compite: `coste_g` en Dijkstra y `g + h`
        ///              en A*. Debe ser no negativo.
        /// @pre `clave` no cae en un cubo ya consumido, es decir, no es menor que
        ///      la base del último cubo extraído. Dijkstra lo garantiza: el coste
        ///      de un hijo nunca es menor que el de su padre, y el padre salió de
        ///      ese cubo. Si se insertara una clave menor, el nodo quedaría por
        ///      detrás del cursor y se extraería fuera de orden.
        void insertar(std::uint32_t nodo, std::int64_t clave)
        {
            assert(clave >= 0 && "las claves son costes: no pueden ser negativas");

            if (cubos_.empty())
            {
                // Primera inserción: fija la base. Se alinea hacia abajo al
                // múltiplo de la anchura para que los límites de los cubos sean
                // estables aunque la primera clave no sea múltiplo.
                base_ = alinear(clave);
                crecer(1);
            }

            // Ojo: la cota es la base del cubo donde está el cursor, no
            // `clave_min()`, porque `clave_min()` saltaría los cubos vacíos y
            // devolvería una cota demasiado alta para un hijo del nodo que se
            // acaba de extraer.
            assert(clave >= cota_actual() &&
                   "las claves no pueden caer en un cubo ya consumido");

            const std::size_t indice = indice_de(clave);
            if (indice >= cubos_.size())
            {
                crecer(indice + 1);
            }

            cubos_[indice].push_back(nodo);
            ++total_;
        }

        /// Clave mínima que puede haber en la lista (cota inferior).
        ///
        /// Con `anchura() == 1` es exactamente la clave del primer nodo que
        /// saldría. Con `anchura() > 1` es el límite inferior de su cubo, que es
        /// lo único necesario para la regla de parada (nunca sobreestima).
        ///
        /// No es `const` porque avanza el cursor de cubos vacíos (trabajo
        /// amortizado que también hace `extraer_min`).
        ///
        /// @pre La lista no está vacía.
        std::int64_t clave_min()
        {
            avanzar();
            assert(!vacia() && "clave_min() sobre una lista vacia");
            return cota_actual();
        }

        /// Extrae y devuelve el nodo del primer cubo no vacío (LIFO dentro del
        /// cubo). No comprueba si el nodo sigue siendo útil: eso es cosa de la
        /// búsqueda, que mira `kFlagSuperado`.
        ///
        /// @pre La lista no está vacía.
        std::uint32_t extraer_min()
        {
            avanzar();
            assert(!vacia() && "extraer_min() sobre una lista vacia");

            // Se saca por el final (LIFO) y se deja el cubo vacío para que el
            // siguiente `avanzar` lo salte. La capacidad del cubo se conserva:
            // volver a llenarlo no vuelve a reservar memoria.
            std::vector<std::uint32_t> &cubo = cubos_[primero_];
            const std::uint32_t nodo = cubo.back();
            cubo.pop_back();
            --total_;
            return nodo;
        }

        /// ¿No queda ningún nodo encolado?
        bool vacia() const noexcept { return total_ == 0; }

        /// Número de nodos encolados.
        std::size_t size() const noexcept { return total_; }

        /// Número de cubos materializados (crece duplicando y nunca encoge).
        std::size_t n_cubos() const noexcept { return cubos_.size(); }

        /// Clave alineada del cubo 0 (0 mientras no se haya insertado nada).
        std::int64_t base() const noexcept { return base_; }

        /// Céntimos que agrupa cada cubo (1 = orden exacto).
        std::int64_t anchura() const noexcept { return anchura_; }

        /// Bytes comprometidos: cabeceras de los cubos más la capacidad de sus
        /// vectores. Se calcula a demanda (es una métrica, no un dato caliente).
        std::size_t memoria_reservada() const noexcept
        {
            std::size_t bytes = cubos_.capacity() * sizeof(std::vector<std::uint32_t>);
            for (const std::vector<std::uint32_t> &cubo : cubos_)
            {
                bytes += cubo.capacity() * sizeof(std::uint32_t);
            }
            return bytes;
        }

    private:
        /// Avanza `primero_` hasta el primer cubo no vacío.
        ///
        /// Es el único trabajo no constante de la estructura, pero está
        /// amortizado: cada cubo se salta una sola vez en toda la búsqueda.
        void avanzar() noexcept
        {
            while (primero_ < cubos_.size() && cubos_[primero_].empty())
            {
                ++primero_;
            }
        }

        /// Clave alineada del cubo donde está el cursor.
        ///
        /// Es la cota inferior de todo lo que queda en la lista y, a la vez, la
        /// frontera por debajo de la cual ya no se puede insertar nada.
        std::int64_t cota_actual() const noexcept
        {
            return base_ + static_cast<std::int64_t>(primero_) * anchura_;
        }

        /// Índice del cubo de `clave`, relativo a `base_`.
        std::size_t indice_de(std::int64_t clave) const noexcept
        {
            const std::int64_t desplazamiento = clave - base_;
            // Con anchura 1 (el caso normal) no se divide: la rama es predecible
            // y evita la división entera del camino caliente.
            if (anchura_ == 1)
            {
                return static_cast<std::size_t>(desplazamiento);
            }
            return static_cast<std::size_t>(desplazamiento / anchura_);
        }

        /// Alinea `clave` hacia abajo al múltiplo de `anchura_`.
        std::int64_t alinear(std::int64_t clave) const noexcept
        {
            if (anchura_ == 1)
            {
                return clave;
            }
            return (clave / anchura_) * anchura_;
        }

        /// Garantiza al menos `necesarios` cubos, duplicando para que el coste
        /// sea amortizado constante.
        void crecer(std::size_t necesarios)
        {
            const std::size_t doble = cubos_.empty() ? 16 : cubos_.size() * 2;
            cubos_.resize(std::max(necesarios, doble));
        }

        /// Céntimos por cubo (1 = un cubo por céntimo).
        std::int64_t anchura_ = 1;

        /// Clave alineada del cubo 0; se fija con la primera inserción.
        std::int64_t base_ = 0;

        /// Índice del primer cubo no vacío (cursor que solo avanza).
        std::size_t primero_ = 0;

        /// Nodos encolados.
        std::size_t total_ = 0;

        /// Un vector de índices de nodo por cubo.
        std::vector<std::vector<std::uint32_t>> cubos_;
    };

} // namespace referee_scheduler::algorithms
