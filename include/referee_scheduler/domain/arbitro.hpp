// Entidad de dominio `Arbitro`: los datos de un árbitro que puede cubrir plazas.
// Es header-only y sin lógica, como el resto de `domain/`: solo campos públicos
// con valores por defecto que rellenará el futuro cargador de instancias.
// Las referencias a otras entidades son ids `int` (domicilio, equipos vetados) y
// las marcas de tiempo son `time_t`, que después se convierten a minutos del día
// en la fase de datos estáticos.

#pragma once

#include <ctime>
#include <vector>

namespace referee_scheduler::domain {

/// Representa a un árbitro que puede ser asignado a las plazas de los partidos.
class Arbitro {
public:
    /// Identificador único del árbitro.
    int id = 0;
    /// Nivel o categoría del árbitro.
    int nivel = 0;

    /// Identificador de la ubicación de su domicilio real.
    ///
    /// Apunta a una `Sede` de `domain::Instancia` que representa su casa, con
    /// coordenadas, y que **ningún `Partido` usa como sede de partido**. Las
    /// sedes de partido y los domicilios son conjuntos disjuntos: la fase 3 lo
    /// comprueba con un `assert`. Esa disyunción es lo que permite saber, con
    /// solo mirar el estado, si el árbitro todavía no ha salido de casa.
    int domicilio_id = 0;

    /// Identificadores de los equipos que el árbitro no puede arbitrar.
    std::vector<int> vetos;
    /// Inicio de la jornada declarada por el árbitro.
    std::time_t disponibilidad_inicio = 0;
    /// Fin de la jornada declarada por el árbitro.
    std::time_t disponibilidad_fin = 0;
};

}
