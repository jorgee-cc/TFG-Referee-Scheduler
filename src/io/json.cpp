// Implementación del parser JSON mínimo (DOM) y de las utilidades de escapado.
//
// El parser es recursivo descendente y de una sola pasada. No valida semántica
// de la instancia: solo sintaxis JSON. El cargador de instancias es quien
// comprueba tipos, referencias y rangos con mensajes contextualizados.
//
// Decisiones de implementación:
//  - los números se leen con `std::strtod` sobre una copia del fragmento, pero
//    antes se valida la gramática JSON para que `strtod` no acepte cosas como
//    `+1`, `.5`, `01` o `1.`;
//  - los textos se guardan en UTF-8. Los escapes `\uXXXX` se convierten a UTF-8,
//    incluidas las parejas sustitutas de los caracteres fuera del BMP;
//  - las claves duplicadas de un objeto son un error;
//  - hay un límite de profundidad para que una entrada maliciosa no desborde
//    la pila del proceso.

#include "referee_scheduler/io/json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace referee_scheduler::io
{

    ErrorJson::ErrorJson(std::string mensaje,
                         std::size_t posicion,
                         std::size_t linea,
                         std::size_t columna)
        : std::runtime_error(mensaje + " (línea " + std::to_string(linea) +
                             ", columna " + std::to_string(columna) + ")"),
          posicion_(posicion),
          linea_(linea),
          columna_(columna)
    {
    }

    ValorJson ValorJson::booleano(bool valor)
    {
        ValorJson v;
        v.tipo_ = Tipo::Booleano;
        v.booleano_ = valor;
        return v;
    }

    ValorJson ValorJson::numero(double valor)
    {
        ValorJson v;
        v.tipo_ = Tipo::Numero;
        v.numero_ = valor;
        return v;
    }

    ValorJson ValorJson::texto(std::string valor)
    {
        ValorJson v;
        v.tipo_ = Tipo::Texto;
        v.texto_ = std::move(valor);
        return v;
    }

    ValorJson ValorJson::array(Array valor)
    {
        ValorJson v;
        v.tipo_ = Tipo::Array;
        v.array_ = std::move(valor);
        return v;
    }

    ValorJson ValorJson::objeto(Objeto valor)
    {
        ValorJson v;
        v.tipo_ = Tipo::Objeto;
        v.objeto_ = std::move(valor);
        return v;
    }

    const ValorJson *ValorJson::buscar(std::string_view clave) const noexcept
    {
        if (tipo_ != Tipo::Objeto)
        {
            return nullptr;
        }

        for (const auto &[nombre, valor] : objeto_)
        {
            if (nombre == clave)
            {
                return &valor;
            }
        }

        return nullptr;
    }

    namespace
    {

        /// Profundidad máxima de objetos/arrays anidados.
        constexpr int kMaxProfundidad = 256;

        /// Añade un punto de código Unicode al texto como UTF-8.
        void anadir_utf8(std::string &salida, std::uint32_t punto)
        {
            if (punto <= 0x7F)
            {
                salida.push_back(static_cast<char>(punto));
            }
            else if (punto <= 0x7FF)
            {
                salida.push_back(static_cast<char>(0xC0 | (punto >> 6)));
                salida.push_back(static_cast<char>(0x80 | (punto & 0x3F)));
            }
            else if (punto <= 0xFFFF)
            {
                salida.push_back(static_cast<char>(0xE0 | (punto >> 12)));
                salida.push_back(static_cast<char>(0x80 | ((punto >> 6) & 0x3F)));
                salida.push_back(static_cast<char>(0x80 | (punto & 0x3F)));
            }
            else
            {
                salida.push_back(static_cast<char>(0xF0 | (punto >> 18)));
                salida.push_back(static_cast<char>(0x80 | ((punto >> 12) & 0x3F)));
                salida.push_back(static_cast<char>(0x80 | ((punto >> 6) & 0x3F)));
                salida.push_back(static_cast<char>(0x80 | (punto & 0x3F)));
            }
        }

        /// Parser recursivo descendente de un documento JSON.
        class Parser
        {
        public:
            explicit Parser(std::string_view texto) : texto_(texto) {}

            ValorJson parsear_documento()
            {
                // Un BOM UTF-8 al principio es habitual en ficheros editados en
                // Windows; no forma parte del JSON pero no aporta nada rechazarlo.
                if (texto_.size() >= 3 &&
                    static_cast<unsigned char>(texto_[0]) == 0xEF &&
                    static_cast<unsigned char>(texto_[1]) == 0xBB &&
                    static_cast<unsigned char>(texto_[2]) == 0xBF)
                {
                    posicion_ = 3;
                }

                saltar_espacios();
                if (posicion_ >= texto_.size())
                {
                    fallo("el documento está vacío");
                }

                ValorJson valor = parsear_valor();
                saltar_espacios();
                if (posicion_ != texto_.size())
                {
                    fallo("hay contenido después del valor principal");
                }
                return valor;
            }

        private:
            /// Lanza un `ErrorJson` con la línea y columna actuales.
            [[noreturn]] void fallo(const std::string &mensaje) const
            {
                std::size_t linea = 1;
                std::size_t columna = 1;
                for (std::size_t i = 0; i < posicion_ && i < texto_.size(); ++i)
                {
                    if (texto_[i] == '\n')
                    {
                        ++linea;
                        columna = 1;
                    }
                    else
                    {
                        ++columna;
                    }
                }
                throw ErrorJson(mensaje, posicion_, linea, columna);
            }

            void saltar_espacios() noexcept
            {
                while (posicion_ < texto_.size())
                {
                    const char c = texto_[posicion_];
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                    {
                        break;
                    }
                    ++posicion_;
                }
            }

            bool consumir(char esperado) noexcept
            {
                if (posicion_ < texto_.size() && texto_[posicion_] == esperado)
                {
                    ++posicion_;
                    return true;
                }
                return false;
            }

            void esperar(char esperado, const char *descripcion)
            {
                if (!consumir(esperado))
                {
                    fallo(std::string("se esperaba ") + descripcion);
                }
            }

            void consumir_literal(std::string_view literal)
            {
                if (texto_.substr(posicion_, literal.size()) != literal)
                {
                    fallo("literal JSON no válido");
                }
                posicion_ += literal.size();
            }

            ValorJson parsear_valor()
            {
                saltar_espacios();
                if (posicion_ >= texto_.size())
                {
                    fallo("se esperaba un valor");
                }

                switch (texto_[posicion_])
                {
                case '{':
                    return parsear_objeto();
                case '[':
                    return parsear_array();
                case '"':
                    return ValorJson::texto(parsear_cadena());
                case 't':
                    consumir_literal("true");
                    return ValorJson::booleano(true);
                case 'f':
                    consumir_literal("false");
                    return ValorJson::booleano(false);
                case 'n':
                    consumir_literal("null");
                    return ValorJson();
                default:
                    if (texto_[posicion_] == '-' ||
                        (texto_[posicion_] >= '0' && texto_[posicion_] <= '9'))
                    {
                        return ValorJson::numero(parsear_numero());
                    }
                    fallo("carácter no válido al principio de un valor");
                }
            }

            ValorJson parsear_array()
            {
                esperar('[', "'['");
                ++profundidad_;
                if (profundidad_ > kMaxProfundidad)
                {
                    fallo("demasiados niveles de anidamiento");
                }

                ValorJson::Array valores;
                saltar_espacios();
                if (consumir(']'))
                {
                    --profundidad_;
                    return ValorJson::array(std::move(valores));
                }

                while (true)
                {
                    valores.push_back(parsear_valor());
                    saltar_espacios();
                    if (consumir(']'))
                    {
                        break;
                    }
                    esperar(',', "',' o ']'");
                }

                --profundidad_;
                return ValorJson::array(std::move(valores));
            }

            ValorJson parsear_objeto()
            {
                esperar('{', "'{'");
                ++profundidad_;
                if (profundidad_ > kMaxProfundidad)
                {
                    fallo("demasiados niveles de anidamiento");
                }

                ValorJson::Objeto campos;
                saltar_espacios();
                if (consumir('}'))
                {
                    --profundidad_;
                    return ValorJson::objeto(std::move(campos));
                }

                while (true)
                {
                    saltar_espacios();
                    if (posicion_ >= texto_.size() || texto_[posicion_] != '"')
                    {
                        fallo("se esperaba una clave de objeto entre comillas");
                    }
                    std::string clave = parsear_cadena();

                    // Las claves duplicadas casi siempre son una errata; se
                    // rechazan en lugar de quedarse con la última.
                    for (const auto &[nombre, valor] : campos)
                    {
                        (void)valor;
                        if (nombre == clave)
                        {
                            fallo("clave duplicada en el objeto: " + clave);
                        }
                    }

                    saltar_espacios();
                    esperar(':', "':'");

                    ValorJson valor = parsear_valor();
                    campos.emplace_back(std::move(clave), std::move(valor));

                    saltar_espacios();
                    if (consumir('}'))
                    {
                        break;
                    }
                    esperar(',', "',' o '}'");
                }

                --profundidad_;
                return ValorJson::objeto(std::move(campos));
            }

            /// Lee una cadena JSON; se llama con la comilla de apertura delante.
            std::string parsear_cadena()
            {
                esperar('"', "'\"'");

                std::string salida;
                while (posicion_ < texto_.size())
                {
                    const unsigned char c =
                        static_cast<unsigned char>(texto_[posicion_++]);

                    if (c == '"')
                    {
                        return salida;
                    }

                    // Los caracteres de control deben ir escapados en JSON.
                    if (c < 0x20)
                    {
                        fallo("carácter de control sin escapar en una cadena");
                    }

                    if (c != '\\')
                    {
                        salida.push_back(static_cast<char>(c));
                        continue;
                    }

                    if (posicion_ >= texto_.size())
                    {
                        fallo("escape incompleto al final de una cadena");
                    }

                    const char escape = texto_[posicion_++];
                    switch (escape)
                    {
                    case '"':
                        salida.push_back('"');
                        break;
                    case '\\':
                        salida.push_back('\\');
                        break;
                    case '/':
                        salida.push_back('/');
                        break;
                    case 'b':
                        salida.push_back('\b');
                        break;
                    case 'f':
                        salida.push_back('\f');
                        break;
                    case 'n':
                        salida.push_back('\n');
                        break;
                    case 'r':
                        salida.push_back('\r');
                        break;
                    case 't':
                        salida.push_back('\t');
                        break;
                    case 'u':
                    {
                        const std::uint32_t primero = leer_cuatro_hex();
                        std::uint32_t punto = primero;

                        if (primero >= 0xD800 && primero <= 0xDBFF)
                        {
                            // Pareja sustituta: hace falta el segundo \u.
                            if (posicion_ + 1 >= texto_.size() ||
                                texto_[posicion_] != '\\' ||
                                texto_[posicion_ + 1] != 'u')
                            {
                                fallo("falta la segunda mitad de una pareja \\u");
                            }
                            posicion_ += 2;

                            const std::uint32_t segundo = leer_cuatro_hex();
                            if (segundo < 0xDC00 || segundo > 0xDFFF)
                            {
                                fallo("segunda mitad de pareja \\u no válida");
                            }

                            punto = 0x10000 + ((primero - 0xD800) << 10) +
                                    (segundo - 0xDC00);
                        }
                        else if (primero >= 0xDC00 && primero <= 0xDFFF)
                        {
                            fallo("mitad baja de pareja \\u sin mitad alta");
                        }

                        anadir_utf8(salida, punto);
                        break;
                    }
                    default:
                        fallo("escape no válido en una cadena");
                    }
                }

                fallo("cadena sin cerrar");
            }

            std::uint32_t leer_cuatro_hex()
            {
                std::uint32_t valor = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (posicion_ >= texto_.size())
                    {
                        fallo("escape \\u incompleto");
                    }

                    const char c = texto_[posicion_++];
                    std::uint32_t digito = 0;
                    if (c >= '0' && c <= '9')
                    {
                        digito = static_cast<std::uint32_t>(c - '0');
                    }
                    else if (c >= 'a' && c <= 'f')
                    {
                        digito = static_cast<std::uint32_t>(c - 'a' + 10);
                    }
                    else if (c >= 'A' && c <= 'F')
                    {
                        digito = static_cast<std::uint32_t>(c - 'A' + 10);
                    }
                    else
                    {
                        fallo("dígito hexadecimal no válido en \\u");
                    }
                    valor = (valor << 4) | digito;
                }
                return valor;
            }

            double parsear_numero()
            {
                const std::size_t inicio = posicion_;

                // Signo.
                consumir('-');

                // Parte entera: `0` o un dígito 1-9 seguido de más dígitos.
                if (posicion_ >= texto_.size())
                {
                    fallo("número incompleto");
                }
                if (texto_[posicion_] == '0')
                {
                    ++posicion_;
                }
                else if (texto_[posicion_] >= '1' && texto_[posicion_] <= '9')
                {
                    ++posicion_;
                    while (posicion_ < texto_.size() &&
                           texto_[posicion_] >= '0' && texto_[posicion_] <= '9')
                    {
                        ++posicion_;
                    }
                }
                else
                {
                    fallo("se esperaba un dígito");
                }

                // Parte fraccionaria.
                if (posicion_ < texto_.size() && texto_[posicion_] == '.')
                {
                    ++posicion_;
                    if (posicion_ >= texto_.size() ||
                        texto_[posicion_] < '0' || texto_[posicion_] > '9')
                    {
                        fallo("falta un dígito después del punto decimal");
                    }
                    while (posicion_ < texto_.size() &&
                           texto_[posicion_] >= '0' && texto_[posicion_] <= '9')
                    {
                        ++posicion_;
                    }
                }

                // Exponente.
                if (posicion_ < texto_.size() &&
                    (texto_[posicion_] == 'e' || texto_[posicion_] == 'E'))
                {
                    ++posicion_;
                    if (posicion_ < texto_.size() &&
                        (texto_[posicion_] == '+' || texto_[posicion_] == '-'))
                    {
                        ++posicion_;
                    }
                    if (posicion_ >= texto_.size() ||
                        texto_[posicion_] < '0' || texto_[posicion_] > '9')
                    {
                        fallo("falta un dígito en el exponente");
                    }
                    while (posicion_ < texto_.size() &&
                           texto_[posicion_] >= '0' && texto_[posicion_] <= '9')
                    {
                        ++posicion_;
                    }
                }

                const std::string fragmento(texto_.substr(inicio, posicion_ - inicio));
                char *fin = nullptr;
                errno = 0;
                const double valor = std::strtod(fragmento.c_str(), &fin);

                if (fin != fragmento.c_str() + fragmento.size() || errno == ERANGE ||
                    !std::isfinite(valor))
                {
                    fallo("número fuera de rango");
                }

                return valor;
            }

            std::string_view texto_;
            std::size_t posicion_ = 0;
            int profundidad_ = 0;
        };

    } // namespace

    ValorJson parsear_json(std::string_view texto)
    {
        Parser parser(texto);
        return parser.parsear_documento();
    }

    std::string escapar_cadena_json(std::string_view texto)
    {
        std::string salida;
        salida.reserve(texto.size() + 8);

        for (const unsigned char c : texto)
        {
            switch (c)
            {
            case '"':
                salida += "\\\"";
                break;
            case '\\':
                salida += "\\\\";
                break;
            case '\b':
                salida += "\\b";
                break;
            case '\f':
                salida += "\\f";
                break;
            case '\n':
                salida += "\\n";
                break;
            case '\r':
                salida += "\\r";
                break;
            case '\t':
                salida += "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    // Caracteres de control restantes: se escriben como \u00XX.
                    static constexpr char hex[] = "0123456789abcdef";
                    salida += "\\u00";
                    salida.push_back(hex[(c >> 4) & 0x0F]);
                    salida.push_back(hex[c & 0x0F]);
                }
                else
                {
                    salida.push_back(static_cast<char>(c));
                }
                break;
            }
        }

        return salida;
    }

    std::string comillas_json(std::string_view texto)
    {
        return "\"" + escapar_cadena_json(texto) + "\"";
    }

} // namespace referee_scheduler::io
