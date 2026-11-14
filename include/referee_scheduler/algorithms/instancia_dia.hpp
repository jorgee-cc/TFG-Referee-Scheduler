// Tipos de datos estáticos de UNA jornada: el árbitro ya compactado, la matriz
// de distancias entre sus sedes y, más adelante, las plazas y las listas de
// candidatos. Indexar por `0..N_dia-1` (en vez de por el id global) es lo que
// permite que `Estado` y `Arena` no guarden ranuras de árbitros que ese día no
// trabajan.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "referee_scheduler/algorithms/proveedor_distancias.hpp"
#include "referee_scheduler/algorithms/tiempo.hpp"

namespace referee_scheduler::algorithms
{

    /// Árbitro de una jornada concreta, ya compactado.
    ///
    /// `InstanciaDia` guarda estos registros en un vector y el índice dentro del
    /// vector **es** el identificador compacto (`0..N_dia-1`) que usan el estado
    /// y la arena. `id_global` conserva el id original de `domain::Arbitro` para
    /// poder reconstruir la solución al final.
    ///
    /// Solo contiene atributos estáticos (los que se pueden conocer antes de
    /// buscar): no hay historial, porque el estado empaquetado no lo guarda.
    struct ArbitroDia
    {
        /// Id del árbitro en `domain::Instancia`; el índice del vector es el id compacto.
        int id_global = 0;

        /// Nivel del árbitro. Una plaza es candidata si `nivel >= nivel_requerido`.
        int nivel = 0;

        /// Id global de su sede-domicilio. La fase 3 lo traduce al índice
        /// compacto de la matriz de distancias.
        int sede_domicilio_global = 0;

        /// Índice compacto de su sede-domicilio en `InstanciaDia::sede_global`.
        ///
        /// Lo rellena la fase 3. Antes de construir la matriz vale 0 y no debe
        /// usarse: el domicilio global sigue en `sede_domicilio_global`.
        std::uint16_t sede_domicilio = 0;

        /// Primer minuto del día en que puede empezar (0 si ya estaba disponible
        /// antes de que empezara la jornada). Se interpreta como "disponible en
        /// su domicilio desde este minuto".
        int primer_inicio = 0;

        /// Último minuto del día en que puede **empezar** un partido
        /// (`kMinutosPorDia` = 1440 si su disponibilidad cubre el día entero).
        ///
        /// No es la hora a la que debe acabar: un árbitro disponible hasta las
        /// 23:00 sí puede coger un partido que empieza a las 23:00 aunque
        /// termine después de medianoche.
        int ultimo_inicio = kMinutosPorDia;

        /// Ids globales de los equipos que no puede arbitrar, limitados a los
        /// equipos que juegan ese día y ordenados para poder usar búsqueda
        /// binaria en el filtro de candidatos.
        std::vector<int> equipos_vetados;
    };

    /// Todos los datos estáticos de una jornada, ya compactados.
    ///
    /// Es lo que consume el buscador: no guarda entidades del dominio, sino
    /// índices densos (`0..n_arbitros-1`, `0..n_sedes-1`) listos para el estado.
    /// Se construye fase a fase; con la fase 3 contiene ya los árbitros del día
    /// y la matriz de distancias entre las sedes implicadas (sedes de los
    /// partidos y domicilios de los árbitros).
    class InstanciaDia
    {
    public:
        /// Fecha civil de la jornada.
        DiaCivil dia;

        /// Árbitros del día, compactados a `0..n_arbitros-1` por id global
        /// ascendente.
        std::vector<ArbitroDia> arbitros;

        /// Id global de cada sede compacta (`0..n_sedes-1`), en orden ascendente.
        std::vector<int> sede_global;

        /// Matriz de minutos `n_sedes × n_sedes` en orden row-major.
        ///
        /// Es **asimétrica**: `viaje(a, b)` y `viaje(b, a)` pueden diferir.
        /// La diagonal es 0 y las parejas sin ruta valen
        /// `kMinutosInalcanzable`.
        std::vector<Minutos> distancias;

        /// Número de árbitros compactados del día.
        std::size_t n_arbitros() const noexcept;

        /// Número de sedes compactadas del día.
        std::size_t n_sedes() const noexcept;

        /// Minutos de viaje de la sede compacta `desde` a la `hasta`.
        ///
        /// No es simétrico. Las dos sedes deben ser índices válidos.
        Minutos viaje(std::size_t desde, std::size_t hasta) const;
    };

} // namespace referee_scheduler::algorithms
