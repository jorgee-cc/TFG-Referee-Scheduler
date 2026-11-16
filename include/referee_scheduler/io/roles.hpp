// Conversión entre los roles del dominio y los nombres que se usan en los
// ficheros JSON. Vive en `io/` porque el dominio no debe saber cómo se escribe
// un rol en un fichero: `domain::Rol` es un enum y el texto es un detalle de
// entrada/salida.
//
// Los nombres canónicos son los que escribe el generador de informes
// (`principal`, `asistente`, `anotador`, `juez_linea`). Al leer se aceptan
// también las variantes con guion o espacio para que el fichero sea cómodo de
// escribir a mano.

#pragma once

#include <optional>
#include <string_view>

#include "referee_scheduler/domain/plaza.hpp"

namespace referee_scheduler::io
{

    /// Nombre canónico del rol, tal y como se escribe en el JSON.
    inline std::string_view texto_de_rol(domain::Rol rol) noexcept
    {
        switch (rol)
        {
        case domain::Rol::Principal:
            return "principal";
        case domain::Rol::Asistente:
            return "asistente";
        case domain::Rol::Anotador:
            return "anotador";
        case domain::Rol::JuezLinea:
            return "juez_linea";
        }
        return "principal";
    }

    /// Interpreta el texto de un rol. Devuelve `std::nullopt` si no se reconoce.
    ///
    /// Acepta `juez_linea`, `juez-linea` y `juez de linea` para el mismo rol;
    /// el resto de nombres solo tiene la forma canónica.
    inline std::optional<domain::Rol> rol_desde_texto(std::string_view texto) noexcept
    {
        if (texto == "principal")
        {
            return domain::Rol::Principal;
        }
        if (texto == "asistente")
        {
            return domain::Rol::Asistente;
        }
        if (texto == "anotador")
        {
            return domain::Rol::Anotador;
        }
        if (texto == "juez_linea" || texto == "juez-linea" || texto == "juez de linea")
        {
            return domain::Rol::JuezLinea;
        }
        return std::nullopt;
    }

} // namespace referee_scheduler::io
