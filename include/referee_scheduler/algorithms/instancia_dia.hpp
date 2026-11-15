// Tipos de datos estáticos de UNA jornada: el árbitro ya compactado, la matriz
// de distancias entre sus sedes, las plazas ordenadas cronológicamente y, más
// adelante, las listas de candidatos. Indexar por `0..N_dia-1` (en vez de por el
// id global) es lo que permite que `Estado` y `Arena` no guarden ranuras de
// árbitros que ese día no trabajan.

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "referee_scheduler/algorithms/proveedor_distancias.hpp"
#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/domain/plaza.hpp"

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

    /// Plaza de una jornada concreta, expandida desde `domain::Plaza` y su
    /// `domain::Partido` y ya lista para el árbol de búsqueda.
    ///
    /// El índice de este registro en `InstanciaDia::plazas` **es el nivel del
    /// árbol**: la búsqueda decide nivel a nivel en orden cronológico, de modo
    /// que una asignación nunca puede colocar a un árbitro en una plaza anterior
    /// al nivel actual. El registro guarda lo que necesitan el generador de
    /// hijos (horario y sede) y la función objetivo (nivel, dinero y rol).
    ///
    /// Es trivially copyable (sin vectores ni punteros) para que la instancia del
    /// día se pueda copiar o mover sin recorrer cada plaza.
    struct PlazaDia
    {
        /// Minuto del día en que empieza el partido; siempre está en
        /// `[0, kMinutosPorDia - 1]` porque el partido pertenece al día en que
        /// empieza.
        int minuto_inicio = 0;

        /// Minuto del día en que acaba el partido, medido desde las 00:00 del
        /// día de la jornada (no del día real de finalización). Puede pasar de
        /// `kMinutosPorDia`: un partido del viernes de 23:30 a 01:00 vale 1500,
        /// no 60 ni 1440. Es el minuto en que el árbitro queda libre para el
        /// siguiente partido.
        ///
        /// Se calcula con `minuto_absoluto` y no con `minuto_en_dia`, porque
        /// `minuto_en_dia` recorta a 1440 y perdería la hora real de final.
        int minuto_fin = 0;

        /// Índice compacto de la sede en `InstanciaDia::sede_global` y en la
        /// matriz de distancias (`0..n_sedes-1`).
        std::uint16_t sede = 0;

        /// Nivel mínimo que debe tener el árbitro. El filtro de candidatos
        /// (fase 5) exige `ArbitroDia::nivel >= nivel_requerido`.
        int nivel_requerido = 0;

        /// Retribución de la plaza en céntimos enteros (ver `a_centimos`).
        ///
        /// Es lo que cuesta dejar la plaza vacía cuando el rol no es principal.
        std::int64_t retribucion_centimos = 0;

        /// Rol que se cubre en la plaza. Solo se usa para saber si la plaza es
        /// obligatoria: `Rol::Principal` no puede quedarse vacía.
        domain::Rol rol = domain::Rol::Principal;

        /// Id global del partido, para reconstruir la solución al final.
        int partido_id = 0;

        /// Id global de la plaza, para reconstruir la solución al final.
        int plaza_id = 0;

        /// Equipos que juegan el partido. La fase 5 descarta a los árbitros que
        /// tengan alguno de ellos como vetado.
        int equipo_local_id = 0;
        int equipo_visitante_id = 0;

        /// ¿Es una plaza de árbitro principal?
        ///
        /// Las principales son obligatorias: `coste_dejar_vacia` devuelve
        /// `std::nullopt` para ellas. El resto puede abandonarse pagando su
        /// retribución.
        bool es_principal() const noexcept
        {
            return rol == domain::Rol::Principal;
        }
    };

    static_assert(std::is_trivially_copyable_v<PlazaDia>,
                  "PlazaDia debe poder copiarse con memcpy");

    /// Todos los datos estáticos de una jornada, ya compactados.
    ///
    /// Es lo que consume el buscador: no guarda entidades del dominio, sino
    /// índices densos (`0..n_arbitros-1`, `0..n_sedes-1`, `0..n_plazas-1`)
    /// listos para el estado. Se construye fase a fase; con la fase 4 contiene ya
    /// los árbitros del día, la matriz de distancias entre las sedes implicadas
    /// (sedes de los partidos y domicilios de los árbitros) y las plazas
    /// ordenadas por hora de inicio.
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

        /// Plazas del día en orden `(minuto_inicio, sede, partido_id,
        /// plaza_id)`: la posición en el vector es el nivel del árbol de
        /// búsqueda. El orden es total y determinista, así que no depende del
        /// orden en que el cargador haya metido partidos y plazas.
        std::vector<PlazaDia> plazas;

        /// Número de árbitros compactados del día.
        std::size_t n_arbitros() const noexcept;

        /// Número de sedes compactadas del día.
        std::size_t n_sedes() const noexcept;

        /// Número de plazas del día (niveles del árbol de búsqueda).
        std::size_t n_plazas() const noexcept;

        /// Minutos de viaje de la sede compacta `desde` a la `hasta`.
        ///
        /// No es simétrico. Las dos sedes deben ser índices válidos.
        Minutos viaje(std::size_t desde, std::size_t hasta) const;
    };

} // namespace referee_scheduler::algorithms
