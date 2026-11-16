// Estado empaquetado de la búsqueda: la mínima estadística suficiente del
// pasado, 4 B por árbitro (sede actual, minuto de liberación y bits reservados)
// con capacidad de compilación para 600 árbitros.
// Es trivially copyable y sin memoria dinámica, de modo que copiar un estado es
// un memcpy de 2400 B; los nodos de la búsqueda los guarda `algorithms::Arena`,
// que solo reserva las `n_arbitros` entradas del día.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace referee_scheduler::algorithms
{

    /// Número máximo de árbitros que puede representar un estado.
    ///
    /// Fijarlo en tiempo de compilación es lo que permite que `Estado` sea
    /// trivially copyable y que copiarlo sea un único memcpy de 2400 B.
    ///
    /// Es solo la **capacidad**: el número real del día es `n_arbitros` (dato de
    /// ejecución) y la búsqueda recorre únicamente `[0, n_arbitros)`. Las ranuras
    /// sobrantes no se leen ni se copian.
    inline constexpr std::size_t kMaxArbitros = 600;

    /// Un árbitro empaquetado en 4 bytes.
    ///
    /// Es la mínima estadística suficiente del pasado para decidir sus
    /// asignaciones futuras: dónde está, cuándo queda libre y un bloque de bits
    /// reservado. No guarda historial (qué partidos hizo), solo el resultado
    /// físico de haberlos hecho.
    ///
    /// Reparto: 10 + 11 + 11 = 32 bits exactos.
    ///   - 10 bits de sede: 0..1023.
    ///   - 11 bits de minuto: 0..2047 (un día completo son 1440).
    ///   - 11 bits reservados: 0..2047, sin uso por ahora.
    ///
    /// En GCC/Clang sobre x86-64 los campos ocupan, de menos a más significativo,
    /// `sede_id`, `minuto_liberacion` y `reservado`. El estándar no garantiza ese
    /// orden; la garantía que nos importa es el tamaño, comprobado abajo.
    struct EstadoArbitro
    {
        /// Sede en la que se encuentra ahora el árbitro.
        ///
        /// En el estado inicial es su **domicilio**, que por construcción nunca
        /// es la sede de un partido (`Arbitro::domicilio_id` y
        /// `Partido::sede_id` son disjuntos). Después de cualquier asignación es
        /// la sede del último partido, nunca su domicilio. Por tanto
        /// `sede_id == domicilio` identifica exactamente el estado inicial y la
        /// función objetivo lo usa para saber que aún no ha salido de casa.
        std::uint32_t sede_id : 10;

        /// Minuto del día en que el árbitro queda libre para el siguiente partido. De las 9:00 a las 23:59 hay 900 minutos, así que 11 bits son suficientes.
        ///
        /// En el estado inicial vale `ArbitroDia::primer_inicio`, y entonces su
        /// significado es "primer minuto en que puede **arbitrar**", no la hora
        /// de salida: puede salir de casa antes para llegar a tiempo. Fuera del
        /// estado inicial es la hora a la que queda libre en la sede en la que
        /// está, y el siguiente desplazamiento empieza en ese minuto.
        std::uint32_t minuto_liberacion : 11;

        /// Bits sin uso, reservados para una restricción futura por árbitro
        /// (por ejemplo, un contador de partidos). Se inicializan a cero.
        std::uint32_t reservado : 11;
    };

    static_assert(sizeof(EstadoArbitro) == 4,
                  "EstadoArbitro debe ocupar exactamente 32 bits");
    static_assert(std::is_trivially_copyable_v<EstadoArbitro>,
                  "EstadoArbitro debe poder copiarse con memcpy");

    /// Estado físico de capacidad máxima: 600 árbitros * 4 B = 2400 B.
    ///
    /// Es la única información dinámica que necesita la búsqueda. Cada árbitro
    /// ocupa 4 B, de modo que 16 árbitros caben exactamente en una línea de
    /// caché de 64 B y el estado completo ocupa 37 líneas y media (38 líneas).
    ///
    /// Este tipo es la vista de **capacidad máxima** (construir el estado inicial,
    /// depurar, tests). El almacenamiento de los nodos de búsqueda lo hace
    /// `algorithms::Arena`, que reserva solo `4 * n_arbitros` bytes por nodo y
    /// nunca toca `[n_arbitros, kMaxArbitros)`.
    struct Estado
    {
        /// Un registro por árbitro, indexado por su id.
        std::array<EstadoArbitro, kMaxArbitros> arbitros{};
    };

    static_assert(sizeof(Estado) == 2400,
                  "Estado debe ocupar exactamente 2400 B");
    static_assert(std::is_trivially_copyable_v<Estado>,
                  "Estado debe poder copiarse con memcpy");

} // namespace referee_scheduler::algorithms
