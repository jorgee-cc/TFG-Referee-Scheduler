// Pruebas del parser JSON y del cargador de instancias (paso 5g).
//
// Cubre:
//  - sintaxis del parser mínimo: objetos, arrays, textos, escapes, números,
//    booleanos, null y errores con línea/columna;
//  - lectura de una instancia válida y de sus tipos de dominio;
//  - validación semántica: ids duplicados, referencias inexistentes,
//    domicilio que coincide con una sede de partido, roles, fechas y
//    retribuciones;
//  - lectura desde un fichero temporal.
//
// Es el ejecutable `referee_scheduler_json_tests` de CTest.

#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/domain/instancia.hpp"
#include "referee_scheduler/io/instancia_json.hpp"
#include "referee_scheduler/io/json.hpp"

namespace
{

    using referee_scheduler::domain::Instancia;
    using referee_scheduler::domain::Rol;
    using referee_scheduler::io::ErrorInstancia;
    using referee_scheduler::io::ErrorJson;
    using referee_scheduler::io::instancia_desde_json;
    using referee_scheduler::io::parsear_json;

    int fallos = 0;

    void comprobar(bool condicion, const char *mensaje)
    {
        if (!condicion)
        {
            std::cerr << "FALLO: " << mensaje << '\n';
            ++fallos;
        }
    }

    /// Construye un `time_t` local, igual que hará el cargador con el texto ISO.
    std::time_t instante(int year, int mes, int dia, int hora, int minuto)
    {
        std::tm partes{};
        partes.tm_year = year - 1900;
        partes.tm_mon = mes - 1;
        partes.tm_mday = dia;
        partes.tm_hour = hora;
        partes.tm_min = minuto;
        partes.tm_isdst = -1;
        return std::mktime(&partes);
    }

    /// Documento completo con las cinco colecciones; cada argumento es el
    /// contenido (sin corchetes) de un array.
    std::string documento(const std::string &equipos,
                          const std::string &sedes,
                          const std::string &arbitros,
                          const std::string &partidos,
                          const std::string &plazas)
    {
        return "{\"equipos\":[" + equipos + "],\"sedes\":[" + sedes +
               "],\"arbitros\":[" + arbitros + "],\"partidos\":[" + partidos +
               "],\"plazas\":[" + plazas + "]}";
    }

    const std::string kEquipos =
        R"({"id":1,"nombre":"Equipo A"},{"id":2,"nombre":"Equipo B"})";
    const std::string kSedes =
        R"({"id":10,"nombre":"Campo","latitud":43.36,"longitud":-8.41},)"
        R"({"id":90,"nombre":"Domicilio","latitud":43.37,"longitud":-8.40})";
    const std::string kArbitros =
        R"({"id":7,"nivel":2,"domicilio_id":90,"vetos":[2],)"
        R"("disponibilidad_inicio":"2026-11-20T08:00:00",)"
        R"("disponibilidad_fin":"2026-11-20T23:00"})";
    const std::string kPartidos =
        R"({"id":100,"sede_id":10,"inicio":"2026-11-20T18:00:00",)"
        R"("final":"2026-11-20T19:45:00","equipo_local_id":1,)"
        R"("equipo_visitante_id":2})";
    const std::string kPlazas =
        R"({"id":1000,"partido_id":100,"rol":"principal",)"
        R"("nivel_requerido":2,"retribucion":45.0},)"
        R"({"id":1001,"partido_id":100,"rol":"asistente",)"
        R"("nivel_requerido":1,"retribucion":25.5})";

    /// Comprueba que una llamada lanza `Excepcion` y que el mensaje contiene
    /// el fragmento esperado.
    template <typename Excepcion, typename Funcion>
    void esperar_error(Funcion &&funcion,
                       std::string_view fragmento,
                       const char *mensaje)
    {
        try
        {
            funcion();
            std::cerr << "FALLO (no lanzó): " << mensaje << '\n';
            ++fallos;
        }
        catch (const Excepcion &excepcion)
        {
            const std::string texto = excepcion.what();
            if (texto.find(fragmento) == std::string::npos)
            {
                std::cerr << "FALLO (mensaje inesperado): " << mensaje << "\n  "
                          << texto << '\n';
                ++fallos;
            }
        }
        catch (...)
        {
            std::cerr << "FALLO (excepción distinta): " << mensaje << '\n';
            ++fallos;
        }
    }

    void probar_parser()
    {
        const auto raiz = parsear_json(
            R"({
                "texto": "hola\nmundo",
                "unicode": "\u00e9",
                "numero": -1.5e2,
                "entero": 42,
                "verdadero": true,
                "nulo": null,
                "lista": [1, 2, {"x": "y"}]
            })");

        comprobar(raiz.es_objeto(), "la raíz debe ser un objeto");
        comprobar(raiz.buscar("texto") != nullptr, "falta la clave texto");
        comprobar(raiz.buscar("texto")->texto() == "hola\nmundo",
                  "escape \\n mal interpretado");
        comprobar(raiz.buscar("unicode")->texto() == "é",
                  "escape \\u mal interpretado");
        comprobar(raiz.buscar("numero")->numero() == -150.0,
                  "número con exponente mal interpretado");
        comprobar(raiz.buscar("entero")->numero() == 42.0,
                  "entero mal interpretado");
        comprobar(raiz.buscar("verdadero")->booleano(),
                  "booleano mal interpretado");
        comprobar(raiz.buscar("nulo")->es_nulo(), "null mal interpretado");
        comprobar(raiz.buscar("lista")->array().size() == 3,
                  "array mal interpretado");

        // Escapes y comillas para el informe.
        comprobar(referee_scheduler::io::escapar_cadena_json("a\"b\\c\n") ==
                      "a\\\"b\\\\c\\n",
                  "escapado de texto JSON incorrecto");
        comprobar(referee_scheduler::io::comillas_json("hola") == "\"hola\"",
                  "comillas JSON incorrectas");

        esperar_error<ErrorJson>([] { parsear_json("{"); }, "línea",
                                 "objeto sin cerrar");
        esperar_error<ErrorJson>([] { parsear_json("[1,]"); }, "línea",
                                 "coma final en array");
        esperar_error<ErrorJson>(
            [] { parsear_json("{\"a\":1,\"a\":2}"); }, "duplicada",
            "clave duplicada");
        esperar_error<ErrorJson>([] { parsear_json("01"); }, "línea",
                                 "cero inicial");
        esperar_error<ErrorJson>([] { parsear_json("\"abc"); }, "cerrar",
                                 "cadena sin cerrar");
        esperar_error<ErrorJson>([] { parsear_json("true false"); },
                                 "contenido", "contenido extra");
        esperar_error<ErrorJson>([] { parsear_json(""); }, "vacío",
                                 "documento vacío");
    }

    void probar_instancia_valida()
    {
        const Instancia instancia = instancia_desde_json(
            documento(kEquipos, kSedes, kArbitros, kPartidos, kPlazas));

        comprobar(instancia.equipos.size() == 2, "número de equipos");
        comprobar(instancia.sedes.size() == 2, "número de sedes");
        comprobar(instancia.arbitros.size() == 1, "número de árbitros");
        comprobar(instancia.partidos.size() == 1, "número de partidos");
        comprobar(instancia.plazas.size() == 2, "número de plazas");

        const auto &arbitro = instancia.arbitros.front();
        comprobar(arbitro.id == 7 && arbitro.nivel == 2,
                  "datos del árbitro");
        comprobar(arbitro.domicilio_id == 90, "domicilio del árbitro");
        comprobar(arbitro.vetos.size() == 1 && arbitro.vetos.front() == 2,
                  "vetos del árbitro");
        comprobar(arbitro.disponibilidad_inicio ==
                      instante(2026, 11, 20, 8, 0),
                  "inicio de disponibilidad");
        comprobar(arbitro.disponibilidad_fin == instante(2026, 11, 20, 23, 0),
                  "fin de disponibilidad");

        const auto &partido = instancia.partidos.front();
        comprobar(partido.sede_id == 10, "sede del partido");
        comprobar(partido.inicio == instante(2026, 11, 20, 18, 0),
                  "inicio del partido");
        comprobar(partido.final == instante(2026, 11, 20, 19, 45),
                  "final del partido");

        comprobar(instancia.plazas[0].rol == Rol::Principal,
                  "rol principal");
        comprobar(instancia.plazas[1].rol == Rol::Asistente,
                  "rol asistente");
        comprobar(instancia.plazas[1].retribucion == 25.5,
                  "retribución de la plaza");
    }

    void probar_errores_de_contenido()
    {
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(
                    R"({"equipos":[],"sedes":[],"arbitros":[],"plazas":[]})");
            },
            "partidos", "falta una colección");

        // Domicilio inexistente.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, R"({"id":10,"latitud":43.36,"longitud":-8.41})",
                    kArbitros, kPartidos, kPlazas));
            },
            "domicilio_id", "domicilio inexistente");

        // Domicilio que también es sede de partido.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos,
                    R"({"id":10,"latitud":43.36,"longitud":-8.41},)"
                    R"({"id":90,"latitud":43.37,"longitud":-8.40})",
                    R"({"id":7,"nivel":2,"domicilio_id":10,)"
                    R"("disponibilidad_inicio":"2026-11-20T08:00",)"
                    R"("disponibilidad_fin":"2026-11-20T23:00"})",
                    kPartidos, kPlazas));
            },
            "también una sede de partido", "domicilio y sede solapados");

        // Rol desconocido.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros, kPartidos,
                    R"({"id":1000,"partido_id":100,"rol":"portero",)"
                    R"("nivel_requerido":2,"retribucion":45.0})"));
            },
            "rol desconocido", "rol inválido");

        // Retribución negativa.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros, kPartidos,
                    R"({"id":1000,"partido_id":100,"rol":"principal",)"
                    R"("nivel_requerido":2,"retribucion":-1})"));
            },
            "no puede ser negativa", "retribución negativa");

        // Fecha imposible: 30 de febrero.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros,
                    R"({"id":100,"sede_id":10,"inicio":"2026-02-30T18:00",)"
                    R"("final":"2026-02-30T19:00","equipo_local_id":1,)"
                    R"("equipo_visitante_id":2})",
                    kPlazas));
            },
            "no existe", "fecha imposible");

        // Identificador duplicado.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    R"({"id":1},{"id":1})", kSedes, kArbitros, kPartidos,
                    kPlazas));
            },
            "duplicado", "id duplicado");

        // Partido sin plaza principal.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros, kPartidos,
                    R"({"id":1001,"partido_id":100,"rol":"asistente",)"
                    R"("nivel_requerido":1,"retribucion":25.0})"));
            },
            "principal", "partido sin principal");

        // Dos plazas con el mismo rol en el mismo partido.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros, kPartidos,
                    R"({"id":1000,"partido_id":100,"rol":"principal",)"
                    R"("nivel_requerido":2,"retribucion":45.0},)"
                    R"({"id":1001,"partido_id":100,"rol":"principal",)"
                    R"("nivel_requerido":1,"retribucion":25.0})"));
            },
            "ya tiene una plaza", "rol duplicado");

        // Fin anterior al inicio.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros,
                    R"({"id":100,"sede_id":10,"inicio":"2026-11-20T19:00",)"
                    R"("final":"2026-11-20T18:00","equipo_local_id":1,)"
                    R"("equipo_visitante_id":2})",
                    kPlazas));
            },
            "final", "partido con fin anterior al inicio");

        // Equipo inexistente en un partido.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos, kSedes, kArbitros,
                    R"({"id":100,"sede_id":10,"inicio":"2026-11-20T18:00",)"
                    R"("final":"2026-11-20T19:00","equipo_local_id":99,)"
                    R"("equipo_visitante_id":2})",
                    kPlazas));
            },
            "equipo_local_id", "equipo inexistente");

        // Sede sin coordenadas.
        esperar_error<ErrorInstancia>(
            [] {
                instancia_desde_json(documento(
                    kEquipos,
                    R"({"id":10,"latitud":0,"longitud":0},)"
                    R"({"id":90,"latitud":43.37,"longitud":-8.40})",
                    kArbitros, kPartidos, kPlazas));
            },
            "sin geolocalizar", "sede sin coordenadas");
    }

    void probar_fichero()
    {
        const auto marca = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        const std::filesystem::path ruta =
            std::filesystem::temp_directory_path() /
            ("referee_scheduler_json_" + std::to_string(marca) + ".json");

        {
            std::ofstream salida(ruta, std::ios::binary);
            salida << documento(kEquipos, kSedes, kArbitros, kPartidos, kPlazas);
        }

        const Instancia instancia =
            referee_scheduler::io::cargar_instancia_json(ruta);
        comprobar(instancia.partidos.size() == 1, "lectura desde fichero");

        std::filesystem::remove(ruta);

        esperar_error<ErrorInstancia>(
            [] {
                referee_scheduler::io::cargar_instancia_json(
                    "/ruta/que/no/existe/instancia.json");
            },
            "no se pudo abrir", "fichero inexistente");
    }

} // namespace

int main()
{
    probar_parser();
    probar_instancia_valida();
    probar_errores_de_contenido();
    probar_fichero();

    if (fallos != 0)
    {
        std::cerr << fallos << " fallo(s)\n";
        return 1;
    }

    std::cout << "test_json: OK\n";
    return 0;
}
