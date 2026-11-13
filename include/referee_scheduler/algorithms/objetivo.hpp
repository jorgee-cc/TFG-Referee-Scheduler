#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>

#include "referee_scheduler/algorithms/estado.hpp"
#include "referee_scheduler/domain/plaza.hpp"

namespace referee_scheduler::algorithms
{

    /// Céntimos que se cobran por cada minuto de tiempo consumido.
    ///
    /// 14 céntimos/minuto equivalen a 8,4 €/hora, el valor acordado para
    /// aproximar un coste de 8 €/hora trabajada.
    inline constexpr std::int64_t kCentimosPorMinuto = 14;

    /// Coste simbólico que se suma a toda asignación.
    ///
    /// Garantiza que cualquier transición cueste al menos 1 céntimo: así la
    /// ruta siempre "se encarece" al avanzar y no aparecen mesetas de coste 0
    /// que rompan el orden de exploración de Dijkstra.
    inline constexpr std::int64_t kEpsilonCentimos = 1;

    /// Parámetros ajustables de la función objetivo.
    ///
    /// Los valores por defecto son los de producción; los tests y los futuros
    /// experimentos pueden variarlos sin tocar el resto del código.
    struct ParametrosObjetivo
    {
        /// Coste de un minuto de viaje o de espera.
        std::int64_t centimos_por_minuto = kCentimosPorMinuto;

        /// Coste fijo que se suma a cada asignación.
        std::int64_t epsilon_centimos = kEpsilonCentimos;
    };

    /// Convierte euros a céntimos enteros.
    ///
    /// Toda la función objetivo trabaja en céntimos para que las sumas y las
    /// comparaciones sean exactas (`int64_t`), sin errores de coma flotante.
    /// Se redondea al céntimo más cercano y se espera llamar una sola vez por
    /// plaza (idealmente al cargar la instancia).
    inline std::int64_t a_centimos(double euros) noexcept
    {
        return static_cast<std::int64_t>(std::llround(euros * 100.0));
    }

    /// Coste de asignar una plaza a un árbitro.
    ///
    /// El coste representa únicamente tiempo consumido: el dinero de la plaza
    /// no se suma aquí (se penaliza solo cuando la plaza queda vacía).
    ///
    /// Fórmula (todo en minutos y céntimos):
    ///
    ///     llegada = arbitro.minuto_liberacion + tiempo_viaje
    ///     espera  = (arbitro.sede_id == sede_domicilio)
    ///                   ? 0                                  // esperar en casa es gratis
    ///                   : minuto_inicio_plaza - llegada
    ///     coste   = epsilon
    ///             + (tiempo_viaje + espera) * centimos_por_minuto
    ///             + (retorno_nuevo - retorno_anterior) * centimos_por_minuto
    ///
    /// El último término es el **ajuste del viaje de retorno**: se cobra la
    /// diferencia entre volver a casa desde la nueva sede y volver desde la
    /// sede anterior. Así cada transición asume que la jornada termina ahí y,
    /// al encadenar asignaciones, los ajustes se compensan entre sí: la suma
    /// telescópica deja al final un único viaje de vuelta a casa desde la
    /// última sede. Puede ser negativo (la nueva sede está más cerca de casa),
    /// pero con una matriz de distancias métrica el total de la transición
    /// nunca baja de `epsilon_centimos`, así que no hay aristas de coste
    /// negativo y Dijkstra sigue siendo aplicable.
    ///
    /// @param parametros          Pesos de tiempo y épsilon.
    /// @param arbitro             Sede actual y minuto en que queda libre.
    /// @param sede_domicilio      Sede en la que vive el árbitro.
    /// @param minuto_inicio_plaza Minuto del día en que arranca el partido.
    /// @param tiempo_viaje        d(sede_actual, sede_plaza), de la matriz.
    /// @param retorno_nuevo       d(sede_plaza, domicilio), de la matriz.
    /// @param retorno_anterior    d(sede_actual, domicilio), de la matriz.
    ///
    /// @return Coste en céntimos, o `std::nullopt` si el árbitro llega tarde
    ///         (`llegada > minuto_inicio_plaza`): esa transición es infactible
    ///         y el generador de hijos no debe crearla. Se devuelve `nullopt`
    ///         en vez de una espera negativa para que llegar tarde no abarate
    ///         nunca la ruta.
    inline std::optional<std::int64_t> coste_asignar(
        const ParametrosObjetivo &parametros,
        const EstadoArbitro &arbitro,
        int sede_domicilio,
        int minuto_inicio_plaza,
        int tiempo_viaje,
        int retorno_nuevo,
        int retorno_anterior) noexcept
    {
        // Los minutos de viaje vienen de la matriz de distancias y
        // no pueden ser negativos; un valor así sería un fallo de programación.
        assert(tiempo_viaje >= 0 && "los minutos de viaje no pueden ser negativos");
        assert(retorno_nuevo >= 0 && "los minutos de retorno no pueden ser negativos");
        assert(retorno_anterior >= 0 && "los minutos de retorno no pueden ser negativos");
        assert(sede_domicilio >= 0 && "la sede del domicilio debe ser valida");
        assert(parametros.centimos_por_minuto >= 0 && "k no puede ser negativo");
        assert(parametros.epsilon_centimos >= 0 && "el epsilon no puede ser negativo");

        // La liberación es un bitfield de 11 bits: cabe de sobra en un int.
        const int minuto_llegada =
            static_cast<int>(arbitro.minuto_liberacion) + tiempo_viaje;

        // Llegar después del inicio es infactible: no se puede arbitrar un
        // partido al que se llega tarde.
        if (minuto_llegada > minuto_inicio_plaza)
        {
            return std::nullopt;
        }

        // Si el árbitro está en su domicilio, esperar no le cuesta nada: está
        // en casa. Si ya está en ruta, cada minuto de espera es tiempo muerto.
        const std::int64_t espera =
            (arbitro.sede_id == static_cast<std::uint32_t>(sede_domicilio))
                ? 0
                : static_cast<std::int64_t>(minuto_inicio_plaza - minuto_llegada);

        const std::int64_t k = parametros.centimos_por_minuto;
        const std::int64_t coste_tiempo =
            (static_cast<std::int64_t>(tiempo_viaje) + espera) * k;
        const std::int64_t ajuste_retorno =
            (static_cast<std::int64_t>(retorno_nuevo) - retorno_anterior) * k;

        return parametros.epsilon_centimos + coste_tiempo + ajuste_retorno;
    }

    /// Coste de dejar una plaza sin cubrir.
    ///
    /// Una plaza principal no puede quedarse vacía: devuelve `std::nullopt`
    /// para que el generador de hijos ni siquiera cree esa rama. Cualquier
    /// otro rol sí puede abandonarse, y su coste es la retribución íntegra
    /// que se pierde (ya convertida a céntimos enteros).
    ///
    /// No se suma épsilon: la retribución perdida ya es un coste positivo y
    /// el épsilon existe solo para garantizar avance en las asignaciones.
    inline std::optional<std::int64_t> coste_dejar_vacia(
        domain::Rol rol,
        std::int64_t retribucion_centimos) noexcept
    {
        if (rol == domain::Rol::Principal)
        {
            return std::nullopt;
        }

        assert(retribucion_centimos >= 0 && "la retribucion no puede ser negativa");
        return retribucion_centimos;
    }

} // namespace referee_scheduler::algorithms
