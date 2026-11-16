// Implementación del cargador de instancias JSON.
//
// El flujo es: leer el fichero -> parsear el documento -> recorrer las cinco
// colecciones -> validar referencias cruzadas. La validación semántica se hace
// después de parsear todas las entidades para que una referencia pueda apuntar a
// una entidad declarada más tarde en el fichero (el orden de los arrays no
// importa).
//
// Todos los errores de datos son `ErrorInstancia` (nunca `assert`): el fichero
// lo escribe una persona y un id mal puesto debe producir un mensaje claro.

#include "referee_scheduler/io/instancia_json.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/io/json.hpp"
#include "referee_scheduler/io/roles.hpp"

namespace referee_scheduler::io
{

    namespace
    {

        using domain::Arbitro;
        using domain::Equipo;
        using domain::Instancia;
        using domain::Partido;
        using domain::Plaza;
        using domain::Sede;

        /// Lanza un error de contenido con la ruta lógica del campo.
        [[noreturn]] void error(std::string_view contexto,
                                std::string_view mensaje)
        {
            throw ErrorInstancia(std::string(contexto) + ": " +
                                 std::string(mensaje));
        }

        /// Contexto de un elemento de un array: `arbitros[3]`.
        std::string contexto_de(std::string_view nombre, std::size_t indice)
        {
            return std::string(nombre) + "[" + std::to_string(indice) + "]";
        }

        /// Campo obligatorio de un objeto.
        const ValorJson &campo_obligatorio(const ValorJson &objeto,
                                           std::string_view nombre,
                                           std::string_view contexto)
        {
            const ValorJson *valor = objeto.buscar(nombre);
            if (valor == nullptr)
            {
                error(contexto,
                      "falta el campo obligatorio '" + std::string(nombre) + "'");
            }
            return *valor;
        }

        /// Campo obligatorio que además debe ser un array.
        const ValorJson::Array &array_obligatorio(const ValorJson &objeto,
                                                  std::string_view nombre,
                                                  std::string_view contexto)
        {
            const ValorJson &valor = campo_obligatorio(objeto, nombre, contexto);
            if (!valor.es_array())
            {
                error(std::string(contexto) + "." + std::string(nombre),
                      "debe ser un array");
            }
            return valor.array();
        }

        int entero(const ValorJson &valor, const std::string &contexto)
        {
            if (!valor.es_numero())
            {
                error(contexto, "debe ser un número entero");
            }

            const double numero = valor.numero();
            if (!std::isfinite(numero) || std::trunc(numero) != numero)
            {
                error(contexto, "debe ser un número entero");
            }

            if (numero < static_cast<double>(std::numeric_limits<int>::min()) ||
                numero > static_cast<double>(std::numeric_limits<int>::max()))
            {
                error(contexto, "está fuera del rango de un entero de 32 bits");
            }

            return static_cast<int>(numero);
        }

        double decimal(const ValorJson &valor, const std::string &contexto)
        {
            if (!valor.es_numero() || !std::isfinite(valor.numero()))
            {
                error(contexto, "debe ser un número finito");
            }
            return valor.numero();
        }

        const std::string &texto(const ValorJson &valor,
                                 std::string_view contexto)
        {
            if (!valor.es_texto())
            {
                error(contexto, "debe ser un texto");
            }
            return valor.texto();
        }

        std::string texto_opcional(const ValorJson &objeto,
                                   std::string_view nombre,
                                   const std::string &contexto)
        {
            const ValorJson *valor = objeto.buscar(nombre);
            if (valor == nullptr)
            {
                return {};
            }
            return texto(*valor, contexto + "." + std::string(nombre));
        }

        std::vector<int> vector_enteros_opcional(const ValorJson &objeto,
                                                 std::string_view nombre,
                                                 const std::string &contexto)
        {
            const ValorJson *valor = objeto.buscar(nombre);
            if (valor == nullptr)
            {
                return {};
            }
            if (!valor->es_array())
            {
                error(contexto + "." + std::string(nombre),
                      "debe ser un array de ids");
            }

            std::vector<int> resultado;
            resultado.reserve(valor->array().size());
            for (std::size_t i = 0; i < valor->array().size(); ++i)
            {
                resultado.push_back(entero(
                    valor->array()[i],
                    contexto + "." + std::string(nombre) + "[" +
                        std::to_string(i) + "]"));
            }
            return resultado;
        }

        /// Comprueba que un id no sea negativo y que no esté repetido.
        void registrar_id(std::unordered_set<int> &vistos,
                          int id,
                          const std::string &contexto)
        {
            if (id < 0)
            {
                error(contexto, "el id no puede ser negativo");
            }
            if (!vistos.insert(id).second)
            {
                error(contexto, "id duplicado: " + std::to_string(id));
            }
        }

        int leer_digitos(std::string_view texto, std::size_t posicion,
                         std::size_t digitos, const std::string &contexto)
        {
            int valor = 0;
            for (std::size_t i = 0; i < digitos; ++i)
            {
                const char c = texto[posicion + i];
                if (c < '0' || c > '9')
                {
                    error(contexto,
                          "fecha ISO no válida: se esperaban dígitos en la "
                          "posición " + std::to_string(posicion + i));
                }
                valor = valor * 10 + (c - '0');
            }
            return valor;
        }

        void exigir_caracter(std::string_view texto, std::size_t posicion,
                             char esperado, const std::string &contexto)
        {
            if (texto[posicion] != esperado)
            {
                error(contexto,
                      std::string("fecha ISO no válida: se esperaba '") +
                          esperado + "' en la posición " +
                          std::to_string(posicion));
            }
        }

        bool es_bisiesto(int year) noexcept
        {
            return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        }

        /// Convierte `YYYY-MM-DDTHH:MM[:SS]` en un `time_t` de hora local.
        ///
        /// Se valida el calendario a mano (mes, día del mes, año bisiesto) para
        /// que `mktime` no normalice una fecha imposible como el 30 de febrero.
        std::time_t instante_desde_texto(std::string_view texto_iso,
                                         const std::string &contexto)
        {
            if (texto_iso.size() != 16 && texto_iso.size() != 19)
            {
                error(contexto,
                      "fecha ISO no válida: se esperaba "
                      "'YYYY-MM-DDTHH:MM' o 'YYYY-MM-DDTHH:MM:SS'");
            }

            const int year = leer_digitos(texto_iso, 0, 4, contexto);
            exigir_caracter(texto_iso, 4, '-', contexto);
            const int mes = leer_digitos(texto_iso, 5, 2, contexto);
            exigir_caracter(texto_iso, 7, '-', contexto);
            const int dia = leer_digitos(texto_iso, 8, 2, contexto);
            exigir_caracter(texto_iso, 10, 'T', contexto);
            const int hora = leer_digitos(texto_iso, 11, 2, contexto);
            exigir_caracter(texto_iso, 13, ':', contexto);
            const int minuto = leer_digitos(texto_iso, 14, 2, contexto);

            int segundo = 0;
            if (texto_iso.size() == 19)
            {
                exigir_caracter(texto_iso, 16, ':', contexto);
                segundo = leer_digitos(texto_iso, 17, 2, contexto);
            }

            if (year < 1900 || year > 9999)
            {
                error(contexto, "el año debe estar entre 1900 y 9999");
            }
            if (mes < 1 || mes > 12)
            {
                error(contexto, "el mes debe estar entre 1 y 12");
            }
            if (hora < 0 || hora > 23)
            {
                error(contexto, "la hora debe estar entre 0 y 23");
            }
            if (minuto < 0 || minuto > 59)
            {
                error(contexto, "los minutos deben estar entre 0 y 59");
            }
            if (segundo < 0 || segundo > 59)
            {
                error(contexto, "los segundos deben estar entre 0 y 59");
            }

            static constexpr std::array<int, 12> dias_por_mes{
                31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int dias_del_mes = dias_por_mes[static_cast<std::size_t>(mes - 1)];
            if (mes == 2 && es_bisiesto(year))
            {
                dias_del_mes = 29;
            }
            if (dia < 1 || dia > dias_del_mes)
            {
                error(contexto,
                      "el día " + std::to_string(dia) + " no existe en el mes " +
                          std::to_string(mes));
            }

            std::tm partes{};
            partes.tm_year = year - 1900;
            partes.tm_mon = mes - 1;
            partes.tm_mday = dia;
            partes.tm_hour = hora;
            partes.tm_min = minuto;
            partes.tm_sec = segundo;
            // Igual que el resto del proyecto: la hora del fichero es local y
            // la biblioteca decide el horario de verano.
            partes.tm_isdst = -1;

            const std::time_t instante = std::mktime(&partes);
            if (instante == static_cast<std::time_t>(-1))
            {
                error(contexto, "la fecha no se puede representar como time_t");
            }
            return instante;
        }

        /// Índice del rol dentro de los arrays de validación.
        std::size_t indice_rol(domain::Rol rol) noexcept
        {
            return static_cast<std::size_t>(rol);
        }

    } // namespace

    domain::Instancia instancia_desde_json(std::string_view texto_json)
    {
        const ValorJson raiz = parsear_json(texto_json);
        if (!raiz.es_objeto())
        {
            throw ErrorInstancia("la raíz del documento debe ser un objeto JSON");
        }

        Instancia instancia;

        // --- Equipos ---------------------------------------------------------
        {
            const ValorJson::Array &lista =
                array_obligatorio(raiz, "equipos", "raíz");
            std::unordered_set<int> ids;
            ids.reserve(lista.size());
            instancia.equipos.reserve(lista.size());

            for (std::size_t i = 0; i < lista.size(); ++i)
            {
                const std::string contexto = contexto_de("equipos", i);
                if (!lista[i].es_objeto())
                {
                    error(contexto, "debe ser un objeto");
                }

                Equipo equipo;
                equipo.id = entero(
                    campo_obligatorio(lista[i], "id", contexto), contexto + ".id");
                registrar_id(ids, equipo.id, contexto + ".id");
                equipo.nombre = texto_opcional(lista[i], "nombre", contexto);
                instancia.equipos.push_back(std::move(equipo));
            }
        }

        // --- Sedes -----------------------------------------------------------
        {
            const ValorJson::Array &lista =
                array_obligatorio(raiz, "sedes", "raíz");
            std::unordered_set<int> ids;
            ids.reserve(lista.size());
            instancia.sedes.reserve(lista.size());

            for (std::size_t i = 0; i < lista.size(); ++i)
            {
                const std::string contexto = contexto_de("sedes", i);
                if (!lista[i].es_objeto())
                {
                    error(contexto, "debe ser un objeto");
                }

                Sede sede;
                sede.id = entero(
                    campo_obligatorio(lista[i], "id", contexto), contexto + ".id");
                registrar_id(ids, sede.id, contexto + ".id");
                sede.nombre = texto_opcional(lista[i], "nombre", contexto);
                sede.latitud = decimal(
                    campo_obligatorio(lista[i], "latitud", contexto),
                    contexto + ".latitud");
                sede.longitud = decimal(
                    campo_obligatorio(lista[i], "longitud", contexto),
                    contexto + ".longitud");

                if (sede.latitud < -90.0 || sede.latitud > 90.0)
                {
                    error(contexto + ".latitud", "debe estar entre -90 y 90");
                }
                if (sede.longitud < -180.0 || sede.longitud > 180.0)
                {
                    error(contexto + ".longitud", "debe estar entre -180 y 180");
                }
                if (sede.latitud == 0.0 && sede.longitud == 0.0)
                {
                    error(contexto,
                          "las coordenadas (0, 0) significan 'sin geolocalizar' "
                          "y no sirven para pedir la matriz de distancias");
                }

                instancia.sedes.push_back(std::move(sede));
            }
        }

        // --- Árbitros --------------------------------------------------------
        {
            const ValorJson::Array &lista =
                array_obligatorio(raiz, "arbitros", "raíz");
            std::unordered_set<int> ids;
            ids.reserve(lista.size());
            instancia.arbitros.reserve(lista.size());

            for (std::size_t i = 0; i < lista.size(); ++i)
            {
                const std::string contexto = contexto_de("arbitros", i);
                if (!lista[i].es_objeto())
                {
                    error(contexto, "debe ser un objeto");
                }

                Arbitro arbitro;
                arbitro.id = entero(
                    campo_obligatorio(lista[i], "id", contexto), contexto + ".id");
                registrar_id(ids, arbitro.id, contexto + ".id");
                arbitro.nivel = entero(
                    campo_obligatorio(lista[i], "nivel", contexto),
                    contexto + ".nivel");
                if (arbitro.nivel < 0)
                {
                    error(contexto + ".nivel", "no puede ser negativo");
                }

                arbitro.domicilio_id = entero(
                    campo_obligatorio(lista[i], "domicilio_id", contexto),
                    contexto + ".domicilio_id");

                arbitro.vetos =
                    vector_enteros_opcional(lista[i], "vetos", contexto);

                arbitro.disponibilidad_inicio = instante_desde_texto(
                    texto(campo_obligatorio(lista[i],
                                            "disponibilidad_inicio",
                                            contexto),
                          contexto + ".disponibilidad_inicio"),
                    contexto + ".disponibilidad_inicio");
                arbitro.disponibilidad_fin = instante_desde_texto(
                    texto(campo_obligatorio(lista[i],
                                            "disponibilidad_fin",
                                            contexto),
                          contexto + ".disponibilidad_fin"),
                    contexto + ".disponibilidad_fin");

                if (arbitro.disponibilidad_fin < arbitro.disponibilidad_inicio)
                {
                    error(contexto,
                          "disponibilidad_fin no puede ser anterior a "
                          "disponibilidad_inicio");
                }

                instancia.arbitros.push_back(std::move(arbitro));
            }
        }

        // --- Partidos --------------------------------------------------------
        {
            const ValorJson::Array &lista =
                array_obligatorio(raiz, "partidos", "raíz");
            std::unordered_set<int> ids;
            ids.reserve(lista.size());
            instancia.partidos.reserve(lista.size());

            for (std::size_t i = 0; i < lista.size(); ++i)
            {
                const std::string contexto = contexto_de("partidos", i);
                if (!lista[i].es_objeto())
                {
                    error(contexto, "debe ser un objeto");
                }

                Partido partido;
                partido.id = entero(
                    campo_obligatorio(lista[i], "id", contexto), contexto + ".id");
                registrar_id(ids, partido.id, contexto + ".id");

                partido.sede_id = entero(
                    campo_obligatorio(lista[i], "sede_id", contexto),
                    contexto + ".sede_id");
                partido.inicio = instante_desde_texto(
                    texto(campo_obligatorio(lista[i], "inicio", contexto),
                          contexto + ".inicio"),
                    contexto + ".inicio");
                partido.final = instante_desde_texto(
                    texto(campo_obligatorio(lista[i], "final", contexto),
                          contexto + ".final"),
                    contexto + ".final");

                if (partido.final < partido.inicio)
                {
                    error(contexto,
                          "final no puede ser anterior a inicio");
                }

                // El estado guarda el minuto de liberación en 11 bits (2047).
                // La fase 4 lo comprueba con un `assert`, pero un fichero con
                // un partido de dos días debe fallar aquí con un mensaje claro.
                const std::int64_t base_dia =
                    algorithms::indice_dia(algorithms::dia_de(partido.inicio)) *
                    algorithms::kMinutosPorDia;
                const std::int64_t minuto_fin =
                    algorithms::minuto_absoluto(partido.final) - base_dia;
                if (minuto_fin >= 2048)
                {
                    error(contexto + ".final",
                          "el partido termina demasiado tarde: el minuto " +
                              std::to_string(minuto_fin) +
                              " no cabe en los 11 bits del estado (máximo 2047)");
                }

                partido.equipo_local_id = entero(
                    campo_obligatorio(lista[i], "equipo_local_id", contexto),
                    contexto + ".equipo_local_id");
                partido.equipo_visitante_id = entero(
                    campo_obligatorio(lista[i], "equipo_visitante_id", contexto),
                    contexto + ".equipo_visitante_id");

                if (partido.equipo_local_id == partido.equipo_visitante_id)
                {
                    error(contexto,
                          "el equipo local y el visitante no pueden ser el mismo");
                }

                instancia.partidos.push_back(std::move(partido));
            }
        }

        // --- Plazas ----------------------------------------------------------
        {
            const ValorJson::Array &lista =
                array_obligatorio(raiz, "plazas", "raíz");
            std::unordered_set<int> ids;
            ids.reserve(lista.size());
            instancia.plazas.reserve(lista.size());

            for (std::size_t i = 0; i < lista.size(); ++i)
            {
                const std::string contexto = contexto_de("plazas", i);
                if (!lista[i].es_objeto())
                {
                    error(contexto, "debe ser un objeto");
                }

                Plaza plaza;
                plaza.id = entero(
                    campo_obligatorio(lista[i], "id", contexto), contexto + ".id");
                registrar_id(ids, plaza.id, contexto + ".id");

                plaza.partido_id = entero(
                    campo_obligatorio(lista[i], "partido_id", contexto),
                    contexto + ".partido_id");

                const std::string &rol_texto = texto(
                    campo_obligatorio(lista[i], "rol", contexto),
                    contexto + ".rol");
                const std::optional<domain::Rol> rol =
                    rol_desde_texto(rol_texto);
                if (!rol)
                {
                    error(contexto + ".rol",
                          "rol desconocido '" + rol_texto +
                              "'; usa principal, asistente, anotador o "
                              "juez_linea");
                }
                plaza.rol = *rol;

                plaza.nivel_requerido = entero(
                    campo_obligatorio(lista[i], "nivel_requerido", contexto),
                    contexto + ".nivel_requerido");
                if (plaza.nivel_requerido < 0)
                {
                    error(contexto + ".nivel_requerido",
                          "no puede ser negativo");
                }

                plaza.retribucion = decimal(
                    campo_obligatorio(lista[i], "retribucion", contexto),
                    contexto + ".retribucion");
                if (plaza.retribucion < 0.0)
                {
                    error(contexto + ".retribucion", "no puede ser negativa");
                }

                instancia.plazas.push_back(std::move(plaza));
            }
        }

        // --- Validación de referencias --------------------------------------

        std::unordered_set<int> ids_equipos;
        ids_equipos.reserve(instancia.equipos.size());
        for (const Equipo &equipo : instancia.equipos)
        {
            ids_equipos.insert(equipo.id);
        }

        std::unordered_set<int> ids_sedes;
        ids_sedes.reserve(instancia.sedes.size());
        for (const Sede &sede : instancia.sedes)
        {
            ids_sedes.insert(sede.id);
        }

        // Sedes de partido y domicilios deben ser conjuntos disjuntos. Es el
        // contrato que permite a `coste_asignar` reconocer el primer viaje con
        // `sede_id == domicilio`.
        std::unordered_set<int> sedes_de_partido;
        sedes_de_partido.reserve(instancia.partidos.size());

        for (std::size_t i = 0; i < instancia.partidos.size(); ++i)
        {
            const Partido &partido = instancia.partidos[i];
            const std::string contexto = contexto_de("partidos", i);

            if (ids_sedes.count(partido.sede_id) == 0)
            {
                error(contexto + ".sede_id",
                      "no existe ninguna sede con id " +
                          std::to_string(partido.sede_id));
            }
            sedes_de_partido.insert(partido.sede_id);

            if (ids_equipos.count(partido.equipo_local_id) == 0)
            {
                error(contexto + ".equipo_local_id",
                      "no existe ningún equipo con id " +
                          std::to_string(partido.equipo_local_id));
            }
            if (ids_equipos.count(partido.equipo_visitante_id) == 0)
            {
                error(contexto + ".equipo_visitante_id",
                      "no existe ningún equipo con id " +
                          std::to_string(partido.equipo_visitante_id));
            }
        }

        for (std::size_t i = 0; i < instancia.arbitros.size(); ++i)
        {
            const Arbitro &arbitro = instancia.arbitros[i];
            const std::string contexto = contexto_de("arbitros", i);

            if (ids_sedes.count(arbitro.domicilio_id) == 0)
            {
                error(contexto + ".domicilio_id",
                      "no existe ninguna sede con id " +
                          std::to_string(arbitro.domicilio_id));
            }
            if (sedes_de_partido.count(arbitro.domicilio_id) != 0)
            {
                error(contexto + ".domicilio_id",
                      "la sede " + std::to_string(arbitro.domicilio_id) +
                          " es un domicilio y también una sede de partido; "
                          "deben ser ubicaciones distintas");
            }

            for (std::size_t v = 0; v < arbitro.vetos.size(); ++v)
            {
                if (ids_equipos.count(arbitro.vetos[v]) == 0)
                {
                    error(contexto + ".vetos[" + std::to_string(v) + "]",
                          "no existe ningún equipo con id " +
                              std::to_string(arbitro.vetos[v]));
                }
            }
        }

        // Plazas: el partido debe existir y cada partido debe tener al menos
        // una plaza y exactamente una principal. Además no se permiten dos
        // plazas con el mismo rol dentro del mismo partido.
        std::unordered_map<int, std::size_t> indice_partido;
        indice_partido.reserve(instancia.partidos.size());
        for (std::size_t i = 0; i < instancia.partidos.size(); ++i)
        {
            indice_partido.emplace(instancia.partidos[i].id, i);
        }

        std::vector<std::array<bool, 4>> roles_por_partido(
            instancia.partidos.size(), std::array<bool, 4>{false, false, false, false});
        std::vector<std::size_t> plazas_por_partido(instancia.partidos.size(), 0);

        for (std::size_t i = 0; i < instancia.plazas.size(); ++i)
        {
            const Plaza &plaza = instancia.plazas[i];
            const std::string contexto = contexto_de("plazas", i);
            const auto partido = indice_partido.find(plaza.partido_id);
            if (partido == indice_partido.end())
            {
                error(contexto + ".partido_id",
                      "no existe ningún partido con id " +
                          std::to_string(plaza.partido_id));
            }

            const std::size_t indice = partido->second;
            const std::size_t rol = indice_rol(plaza.rol);
            if (roles_por_partido[indice][rol])
            {
                error(contexto + ".rol",
                      "el partido " + std::to_string(plaza.partido_id) +
                          " ya tiene una plaza con el rol " +
                          std::string(texto_de_rol(plaza.rol)));
            }
            roles_por_partido[indice][rol] = true;
            ++plazas_por_partido[indice];
        }

        for (std::size_t i = 0; i < instancia.partidos.size(); ++i)
        {
            const std::string contexto = contexto_de("partidos", i);
            if (plazas_por_partido[i] == 0)
            {
                error(contexto,
                      "el partido no tiene ninguna plaza");
            }
            if (!roles_por_partido[i][indice_rol(domain::Rol::Principal)])
            {
                error(contexto,
                      "el partido no tiene una plaza con rol principal");
            }
        }

        return instancia;
    }

    domain::Instancia cargar_instancia_json(const std::filesystem::path &ruta)
    {
        std::ifstream entrada(ruta, std::ios::binary);
        if (!entrada)
        {
            throw ErrorInstancia("no se pudo abrir la instancia: " +
                                 ruta.string());
        }

        std::ostringstream contenido;
        contenido << entrada.rdbuf();
        if (!entrada.good() && !entrada.eof())
        {
            throw ErrorInstancia("no se pudo leer la instancia: " +
                                 ruta.string());
        }

        try
        {
            return instancia_desde_json(contenido.str());
        }
        catch (const ErrorJson &e)
        {
            // Se añade la ruta del fichero; el mensaje del parser ya trae la
            // línea y la columna.
            throw ErrorInstancia(ruta.string() + ": " + e.what());
        }
        catch (const ErrorInstancia &e)
        {
            throw ErrorInstancia(ruta.string() + ": " + e.what());
        }
    }

} // namespace referee_scheduler::io
