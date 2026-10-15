#pragma once

namespace referee_scheduler::domain {

/// Rol que desempeña un árbitro dentro de un partido.
enum class Rol {
    Principal,
    Asistente,
    Anotador,
    JuezLinea,
};

/// Hueco de un partido que debe cubrir un árbitro con un rol concreto.
class Plaza {
public:
    /// Identificador único de la plaza.
    int id = 0;
    /// Identificador del partido al que pertenece la plaza.
    int partido_id = 0;
    /// Rol que se cubre en esta plaza.
    Rol rol = Rol::Principal;
    /// Nivel mínimo requerido para cubrir la plaza.
    int nivel_requerido = 0;
    /// Retribución económica asociada a la plaza.
    double retribucion = 0.0;
};

}
