// Función de coste de la búsqueda, en céntimos enteros y por minimización:
// cuánto cuesta asignar una plaza a un árbitro y cuánto cuesta dejarla vacía.
// El coste mide tiempo (viaje, espera y ajuste del retorno a casa) más un
// épsilon por asignación, de forma que ninguna arista sea negativa y Dijkstra
// siga siendo aplicable. Es header-only, sin memoria dinámica, y no depende de
// `domain::Estado`.

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
    /// Hay dos regímenes, distinguidos por la sede que guarda el estado:
    ///
    /// **1. El árbitro está en su domicilio** (`arbitro.sede_id == domicilio`).
    /// Es el estado inicial: todavía no ha salido de casa. Entonces
    /// `arbitro.minuto_liberacion` es el primer minuto en que puede **arbitrar**
    /// (`primer_inicio`), no la hora a la que puede salir: puede salir antes
    /// para llegar a tiempo, por mucho que tarde el viaje. Lo único que no
    /// puede es empezar un partido antes de estar disponible:
    ///
    ///     factible = minuto_inicio_plaza >= arbitro.minuto_liberacion
    ///     coste    = epsilon + tiempo_viaje * k + (retorno_nuevo - 0) * k
    ///
    /// No hay espera: sale de casa en `minuto_inicio_plaza - tiempo_viaje` y
    /// llega justo al inicio. Esa salida puede ser anterior a
    /// `minuto_liberacion`. El viaje se cobra igual, porque es tiempo
    /// consumido. `retorno_anterior` debe valer `d(domicilio, domicilio) = 0`.
    ///
    /// **2. El árbitro ya ha arbitrado** (`sede_id != domicilio`): está en una
    /// sede y `minuto_liberacion` es la hora a la que queda libre allí, así que
    /// el desplazamiento empieza en ese minuto:
    ///
    ///     llegada  = arbitro.minuto_liberacion + tiempo_viaje
    ///     espera   = minuto_inicio_plaza - llegada
    ///     coste    = epsilon + (tiempo_viaje + espera) * k
    ///                        + (retorno_nuevo - retorno_anterior) * k
    ///
    /// La espera aquí es tiempo muerto (ya no está en casa) y se cobra entera.
    ///
    /// En los dos regímenes el último término es el **ajuste del viaje de
    /// retorno**: se cobra la diferencia entre volver a casa desde la nueva sede
    /// y volver desde la sede anterior. Así cada transición asume que la jornada
    /// termina ahí y, al encadenar asignaciones, los ajustes se compensan entre
    /// sí: la suma telescópica deja al final un único viaje de vuelta a casa
    /// desde la última sede. Puede ser negativo (la nueva sede está más cerca de
    /// casa), pero con una matriz de distancias métrica el total de la
    /// transición nunca baja de `epsilon_centimos`, así que no hay aristas de
    /// coste negativo y Dijkstra sigue siendo aplicable.
    ///
    /// Que `sede_id == domicilio` signifique "estado inicial" es válido porque
    /// los partidos nunca se juegan en un domicilio: la fase 3 exige que las
    /// sedes de partido y los domicilios sean ubicaciones distintas. Después de
    /// cualquier asignación el árbitro está en la sede del partido, nunca en su
    /// casa, así que no hay ambigüedad y no hace falta guardar historial.
    ///
    /// @param parametros          Pesos de tiempo y épsilon.
    /// @param arbitro             Sede actual y minuto en que queda libre.
    /// @param domicilio           Índice compacto del domicilio del árbitro.
    /// @param minuto_inicio_plaza Minuto del día en que arranca el partido.
    /// @param tiempo_viaje        d(sede_actual, sede_plaza), de la matriz.
    /// @param retorno_nuevo       d(sede_plaza, domicilio), de la matriz.
    /// @param retorno_anterior    d(sede_actual, domicilio), de la matriz.
    ///
    /// @return Coste en céntimos, o `std::nullopt` si la transición no es
    ///         factible: en el primer caso, si el partido empieza antes de que
    ///         el árbitro esté disponible; en el segundo, si llega tarde
    ///         (`llegada > minuto_inicio_plaza`). El generador de hijos no debe
    ///         crear esos hijos.
    inline std::optional<std::int64_t> coste_asignar(
        const ParametrosObjetivo &parametros,
        const EstadoArbitro &arbitro,
        int domicilio,
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
        assert(domicilio >= 0 && "el domicilio debe ser valido");
        assert(parametros.centimos_por_minuto >= 0 && "k no puede ser negativo");
        assert(parametros.epsilon_centimos >= 0 && "el epsilon no puede ser negativo");

        // Solo el estado inicial tiene la sede del domicilio: los partidos nunca
        // se juegan en una casa (la fase 3 lo garantiza con un assert).
        const bool en_domicilio =
            arbitro.sede_id == static_cast<std::uint32_t>(domicilio);

        const std::int64_t k = parametros.centimos_por_minuto;

        // El ajuste de retorno es el mismo en los dos regímenes.
        const std::int64_t ajuste_retorno =
            (static_cast<std::int64_t>(retorno_nuevo) - retorno_anterior) * k;

        if (en_domicilio)
        {
            // Todavía no ha salido de casa: `minuto_liberacion` es el primer
            // minuto en que puede arbitrar, no el de salida. Puede salir antes
            // para llegar a tiempo, así que el viaje no limita la factibilidad.
            if (minuto_inicio_plaza < static_cast<int>(arbitro.minuto_liberacion))
            {
                return std::nullopt;
            }

            // Sale de casa en `inicio - viaje` y llega justo: no hay espera que
            // cobrar. El viaje sí, porque es tiempo consumido.
            const std::int64_t coste_viaje =
                static_cast<std::int64_t>(tiempo_viaje) * k;
            return parametros.epsilon_centimos + coste_viaje + ajuste_retorno;
        }

        // Ya ha arbitrado: está en una sede y el viaje empieza cuando queda
        // libre. Llegar después del inicio es infactible.
        const int minuto_llegada =
            static_cast<int>(arbitro.minuto_liberacion) + tiempo_viaje;
        if (minuto_llegada > minuto_inicio_plaza)
        {
            return std::nullopt;
        }

        // Ya no está en casa: la espera hasta el inicio es tiempo muerto y se
        // cobra entera. Con una matriz métrica el total nunca baja del épsilon.
        const std::int64_t espera =
            static_cast<std::int64_t>(minuto_inicio_plaza - minuto_llegada);
        const std::int64_t coste_tiempo =
            (static_cast<std::int64_t>(tiempo_viaje) + espera) * k;

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
