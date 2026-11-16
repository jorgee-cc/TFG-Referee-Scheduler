// Parser JSON mínimo del proyecto.
//
// No se usa una biblioteca externa: las instancias son objetos pequeños con
// arrays de números, textos y booleanos, y el proyecto ya tiene un escáner JSON
// artesanal para OSRM. Este parser construye un árbol de valores (DOM) que el
// cargador de instancias recorre con mensajes de error contextualizados.
//
// Alcance deliberado:
//  - objetos, arrays, textos con escapes y `\uXXXX`, números, `true`, `false`,
//    `null`;
//  - números guardados como `double` (los ids y niveles se validan como enteros
//    al leerlos, igual que `retribucion` se guarda como decimal);
//  - claves duplicadas en un objeto son un error: en un fichero de datos casi
//    siempre significan una errata;
//  - profundidad máxima para no desbordar la pila con entradas malformadas.
//
// La implementación está en `src/io/json.cpp`.

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace referee_scheduler::io
{

    /// Error de sintaxis JSON.
    ///
    /// Incluye la posición absoluta y la línea/columna para que el usuario pueda
    /// corregir el fichero sin adivinar dónde está el fallo. El mensaje completo
    /// se construye en el parser (`std::runtime_error::what()`).
    class ErrorJson : public std::runtime_error
    {
    public:
        ErrorJson(std::string mensaje,
                  std::size_t posicion,
                  std::size_t linea,
                  std::size_t columna);

        /// Desplazamiento en bytes desde el comienzo del texto.
        std::size_t posicion() const noexcept { return posicion_; }

        /// Línea (1-based) donde se detectó el error.
        std::size_t linea() const noexcept { return linea_; }

        /// Columna (1-based) dentro de la línea.
        std::size_t columna() const noexcept { return columna_; }

    private:
        std::size_t posicion_ = 0;
        std::size_t linea_ = 1;
        std::size_t columna_ = 1;
    };

    /// Valor de un documento JSON.
    ///
    /// Es un tipo simple con todos los campos posibles; el `Tipo` indica cuál
    /// está activo. Se evita `std::variant` para que el cargador pueda
    /// inspeccionar el tipo con métodos `es_*()` y para que el árbol sea fácil
    /// de recorrer y de depurar.
    class ValorJson
    {
    public:
        /// Tipo de valor almacenado.
        enum class Tipo
        {
            Nulo,
            Booleano,
            Numero,
            Texto,
            Array,
            Objeto,
        };

        /// Array JSON: conserva el orden de aparición.
        using Array = std::vector<ValorJson>;

        /// Objeto JSON: pares clave-valor en orden de aparición. No se usa
        /// `std::map` porque el parser rechaza claves duplicadas y el orden
        /// original ayuda a leer los errores.
        using Objeto = std::vector<std::pair<std::string, ValorJson>>;

        /// Construye un `null`.
        ValorJson() = default;

        static ValorJson booleano(bool valor);
        static ValorJson numero(double valor);
        static ValorJson texto(std::string valor);
        static ValorJson array(Array valor);
        static ValorJson objeto(Objeto valor);

        Tipo tipo() const noexcept { return tipo_; }

        bool es_nulo() const noexcept { return tipo_ == Tipo::Nulo; }
        bool es_booleano() const noexcept { return tipo_ == Tipo::Booleano; }
        bool es_numero() const noexcept { return tipo_ == Tipo::Numero; }
        bool es_texto() const noexcept { return tipo_ == Tipo::Texto; }
        bool es_array() const noexcept { return tipo_ == Tipo::Array; }
        bool es_objeto() const noexcept { return tipo_ == Tipo::Objeto; }

        /// Valor booleano. El llamador debe haber comprobado `es_booleano()`.
        bool booleano() const noexcept { return booleano_; }

        /// Valor numérico. El llamador debe haber comprobado `es_numero()`.
        double numero() const noexcept { return numero_; }

        /// Texto. El llamador debe haber comprobado `es_texto()`.
        const std::string &texto() const noexcept { return texto_; }

        /// Array. El llamador debe haber comprobado `es_array()`.
        const Array &array() const noexcept { return array_; }

        /// Objeto. El llamador debe haber comprobado `es_objeto()`.
        const Objeto &objeto() const noexcept { return objeto_; }

        /// Busca una clave en un objeto. Devuelve `nullptr` si no existe o si
        /// el valor no es un objeto.
        const ValorJson *buscar(std::string_view clave) const noexcept;

    private:
        Tipo tipo_ = Tipo::Nulo;
        bool booleano_ = false;
        double numero_ = 0.0;
        std::string texto_;
        Array array_;
        Objeto objeto_;
    };

    /// Parsea un documento JSON completo.
    ///
    /// @throws ErrorJson si el texto no es JSON válido o queda contenido después
    ///                   del valor principal.
    ValorJson parsear_json(std::string_view texto);

    /// Escapa el contenido de un texto para poder escribirlo entre comillas
    /// dobles en un JSON. No añade las comillas.
    std::string escapar_cadena_json(std::string_view texto);

    /// Devuelve el texto escapado y entre comillas dobles, listo para un JSON.
    std::string comillas_json(std::string_view texto);

} // namespace referee_scheduler::io
