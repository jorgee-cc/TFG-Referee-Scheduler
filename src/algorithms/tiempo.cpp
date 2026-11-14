// Implementación de las utilidades de tiempo declaradas en `tiempo.hpp`.
// Aquí está la única dependencia de la zona horaria del proyecto: descompone el
// `time_t` en hora local (con `localtime_r` cuando existe) para obtener la
// fecha civil y el minuto del día; el resto son operaciones enteras sobre esa
// fecha, sin volver a consultar el huso.

#include "referee_scheduler/algorithms/tiempo.hpp"

#include <algorithm>
#include <cassert>
#include <ctime>

namespace referee_scheduler::algorithms
{

    namespace
    {

        /// Descompone el instante en hora local.
        ///
        /// Se usa `localtime_r` (reentrante y segura entre hilos) porque la
        /// ejecución de los tres días es en paralelo y `std::localtime` comparte
        /// un buffer estático. En sistemas sin POSIX se recurre a
        /// `std::localtime`, que no es reentrante pero mantiene el código
        /// compilando fuera de Linux.
        std::tm descomponer_local(std::time_t instante)
        {
            std::tm partes{};
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__unix__) || defined(__APPLE__)
            const std::tm *resultado = ::localtime_r(&instante, &partes);
            assert(resultado != nullptr && "localtime_r fallo con un time_t valido");
            (void)resultado;
#else
            partes = *std::localtime(&instante);
#endif
            return partes;
        }

    } // namespace

    DiaCivil dia_de(std::time_t instante)
    {
        const std::tm partes = descomponer_local(instante);

        // `tm_year` cuenta los años desde 1900 y `tm_mon` empieza en 0: se
        // normalizan a año natural y mes 1..12 para que `DiaCivil` sea legible.
        return DiaCivil{
            partes.tm_year + 1900,
            partes.tm_mon + 1,
            partes.tm_mday};
    }

    int minuto_del_dia(std::time_t instante)
    {
        const std::tm partes = descomponer_local(instante);
        return partes.tm_hour * 60 + partes.tm_min;
    }

    std::int64_t indice_dia(DiaCivil dia) noexcept
    {
        // Algoritmo `days_from_civil` de Howard Hinnant: convierte (año, mes,
        // día) en días desde 1970-01-01 sin tablas ni bucles. El año se
        // desplaza para que los años bisiestos empiecen en marzo, y la era de
        // 400 años absorbe la regla gregoriana de los bisiestos.
        const std::int64_t year = dia.year - (dia.mes <= 2 ? 1 : 0);
        const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
        const std::int64_t year_en_era = year - era * 400;               // [0, 399]
        const std::int64_t dia_del_year =
            (153 * (dia.mes + (dia.mes > 2 ? -3 : 9)) + 2) / 5 + dia.dia - 1;
        const std::int64_t dia_en_era =
            year_en_era * 365 + year_en_era / 4 - year_en_era / 100 + dia_del_year;
        return era * 146097 + dia_en_era - 719468;
    }

    std::int64_t minuto_absoluto(std::time_t instante)
    {
        return indice_dia(dia_de(instante)) * kMinutosPorDia +
               minuto_del_dia(instante);
    }

    int minuto_en_dia(std::time_t instante, DiaCivil dia)
    {
        const std::int64_t desplazamiento =
            minuto_absoluto(instante) - indice_dia(dia) * kMinutosPorDia;

        // El recorte hace que una disponibilidad que empieza antes del día
        // valga 0 y una que acaba después valga 1440 ("todo el día").
        return static_cast<int>(
            std::clamp<std::int64_t>(desplazamiento, 0, kMinutosPorDia));
    }

} // namespace referee_scheduler::algorithms
