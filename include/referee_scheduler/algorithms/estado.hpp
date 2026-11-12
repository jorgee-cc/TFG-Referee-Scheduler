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
    /// trivially copyable y que copiarlo sea un único memcpy de 1200 B.
    inline constexpr std::size_t kMaxArbitros = 300;

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
        /// Sede en la que se encuentra ahora el árbitro (su domicilio al empezar).
        std::uint32_t sede_id : 10;

        /// Minuto del día en que el árbitro queda libre para el siguiente partido. De las 9:00 a las 23:59 hay 900 minutos, así que 11 bits son suficientes.
        std::uint32_t minuto_liberacion : 11;

        /// Bits sin uso, reservados para una restricción futura por árbitro
        /// (por ejemplo, un contador de partidos). Se inicializan a cero.
        std::uint32_t reservado : 11;
    };

    static_assert(sizeof(EstadoArbitro) == 4,
                  "EstadoArbitro debe ocupar exactamente 32 bits");
    static_assert(std::is_trivially_copyable_v<EstadoArbitro>,
                  "EstadoArbitro debe poder copiarse con memcpy");

    /// Estado físico completo de un día: 300 árbitros * 4 B = 1200 B.
    ///
    /// Es la única información dinámica que necesita la búsqueda. Cada árbitro
    /// ocupa 4 B, de modo que 16 árbitros caben exactamente en una línea de
    /// caché de 64 B y el estado completo ocupa 19 líneas.
    struct Estado
    {
        /// Un registro por árbitro, indexado por su id.
        std::array<EstadoArbitro, kMaxArbitros> arbitros{};
    };

    static_assert(sizeof(Estado) == 1200,
                  "Estado debe ocupar exactamente 1200 B");
    static_assert(std::is_trivially_copyable_v<Estado>,
                  "Estado debe poder copiarse con memcpy");

} // namespace referee_scheduler::algorithms
