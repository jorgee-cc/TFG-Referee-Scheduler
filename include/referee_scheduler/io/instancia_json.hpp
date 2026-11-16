// Cargador de instancias desde JSON (paso 5g).
//
// Este es el punto que convierte un fichero escrito por una persona en la
// `domain::Instancia` que consumen las fases 1-5. La validación es estricta y
// ocurre aquí, no en la búsqueda: un id que no existe, un domicilio que coincide
// con una sede de partido o una fecha imposible deben fallar con un mensaje que
// diga qué campo está mal, no con un `assert` en mitad de la construcción.
//
// Esquema del documento (todas las colecciones son obligatorias):
//
// {
//   "equipos":  [ {"id": 1, "nombre": "..."} ],
//   "sedes":    [ {"id": 10, "nombre": "...", "latitud": 43.36, "longitud": -8.41} ],
//   "arbitros": [ {"id": 1, "nivel": 2, "domicilio_id": 90, "vetos": [1],
//                  "disponibilidad_inicio": "2026-11-20T08:00:00",
//                  "disponibilidad_fin": "2026-11-20T23:00:00"} ],
//   "partidos": [ {"id": 100, "sede_id": 10,
//                  "inicio": "2026-11-20T18:00:00",
//                  "final": "2026-11-20T19:45:00",
//                  "equipo_local_id": 1, "equipo_visitante_id": 2} ],
//   "plazas":   [ {"id": 1000, "partido_id": 100, "rol": "principal",
//                  "nivel_requerido": 2, "retribucion": 45.0} ]
// }
//
// Las fechas son ISO-8601 en hora local (`YYYY-MM-DDTHH:MM` o con segundos) y
// se interpretan en la zona horaria de la máquina, igual que el resto del
// proyecto. `retribucion` va en euros; la conversión a céntimos la hace la fase
// 4 con `a_centimos`.
//
// La implementación está en `src/io/instancia_json.cpp`.

#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "referee_scheduler/domain/instancia.hpp"

namespace referee_scheduler::io
{

    /// Error de contenido de una instancia (no de sintaxis JSON).
    ///
    /// El mensaje incluye la ruta lógica del campo, por ejemplo
    /// `arbitros[3].domicilio_id`, para que el usuario pueda corregir el fichero
    /// sin revisarlo entero.
    class ErrorInstancia : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    /// Construye una instancia a partir del texto de un documento JSON.
    ///
    /// @throws ErrorJson      si el texto no es JSON válido.
    /// @throws ErrorInstancia si el JSON no cumple el esquema o las referencias
    ///                        entre entidades no son coherentes.
    domain::Instancia instancia_desde_json(std::string_view texto);

    /// Lee un fichero JSON y construye la instancia.
    ///
    /// @throws ErrorInstancia si el fichero no se puede leer.
    /// @throws ErrorJson / ErrorInstancia con el mismo criterio que
    ///         `instancia_desde_json`.
    domain::Instancia cargar_instancia_json(const std::filesystem::path &ruta);

} // namespace referee_scheduler::io
