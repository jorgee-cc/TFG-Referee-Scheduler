// Utilidades de tiempo del proyecto: convierten un `time_t` en fecha civil
// local (`DiaCivil`) y en minutos del día, y permiten comparar instantes sin
// volver a `time_t`.
// Es el único punto que depende de la zona horaria; el resto del código trabaja
// con días y minutos, inmunes a los cambios de huso y de horario de verano.
// La implementación está en `src/algorithms/tiempo.cpp`.

#pragma once

#include <cstdint>
#include <ctime>

namespace referee_scheduler::algorithms
{

    /// Minutos que tiene un día completo.
    ///
    /// Un día son 1440 min, holgadamente dentro de los 11 bits de
    /// `EstadoArbitro::minuto_liberacion` (0..2047). Este valor se usa para
    /// recortar las disponibilidades y para comprobar que ningún minuto se
    /// sale del rango representable.
    inline constexpr int kMinutosPorDia = 1440;

    /// Fecha civil (hora local) sin la hora del día.
    ///
    /// Se usa como clave para agrupar los partidos por jornada. Se descompone
    /// a partir de un `time_t` con la zona horaria local, de modo que "el
    /// viernes" es el viernes que ve el usuario en su reloj, no un desplazamiento
    /// fijo respecto a la época.
    struct DiaCivil
    {
        /// Año natural (por ejemplo 2026).
        int year = 1970;
        /// Mes del 1 al 12.
        int mes = 1;
        /// Día del mes empezando en 1.
        int dia = 1;
    };

    /// Igualdad de fechas civiles (año, mes y día).
    constexpr bool operator==(const DiaCivil &izquierda,
                              const DiaCivil &derecha) noexcept
    {
        return izquierda.year == derecha.year &&
               izquierda.mes == derecha.mes &&
               izquierda.dia == derecha.dia;
    }

    /// Orden lexicográfico de fechas civiles.
    constexpr bool operator<(const DiaCivil &izquierda,
                             const DiaCivil &derecha) noexcept
    {
        if (izquierda.year != derecha.year)
        {
            return izquierda.year < derecha.year;
        }
        if (izquierda.mes != derecha.mes)
        {
            return izquierda.mes < derecha.mes;
        }
        return izquierda.dia < derecha.dia;
    }

    /// Descompone un instante en la fecha civil local que le corresponde.
    ///
    /// Es el único punto del proyecto que depende de la zona horaria: todo lo
    /// demás trabaja con `DiaCivil` o con minutos, que son independientes del
    /// huso. Así un cambio de horario de verano no puede desplazar un partido a
    /// otro día por una resta de `time_t`.
    DiaCivil dia_de(std::time_t instante);

    /// Minuto del día local del instante, en `[0, kMinutosPorDia - 1]`.
    ///
    /// Los segundos se descartan (se redondea hacia abajo): el modelo trabaja
    /// con precisión de minuto. Un partido a las 18:00:30 cuenta como 1080.
    int minuto_del_dia(std::time_t instante);

    /// Número de días transcurridos desde el 1970-01-01 (puede ser negativo).
    ///
    /// Sustituye a dividir un `time_t` entre 86400, que sería incorrecto en
    /// husos no enteros y en los cambios de horario. Es pura aritmética entera
    /// sobre la fecha civil (algoritmo de Howard Hinnant), sin `time_t` ni
    /// zona horaria, y sirve para ordenar y comparar días.
    std::int64_t indice_dia(DiaCivil dia) noexcept;

    /// Minuto absoluto desde la época: `indice_dia * 1440 + minuto_del_dia`.
    ///
    /// Sirve para comparar instantes de días distintos sin volver a `time_t`
    /// (por ejemplo, una disponibilidad que empieza el viernes y acaba el
    /// domingo). No es un instante UTC, es una cuenta de minutos de calendario.
    std::int64_t minuto_absoluto(std::time_t instante);

    /// Minuto del instante relativo al comienzo de `dia`, recortado a
    /// `[0, kMinutosPorDia]`.
    ///
    /// - Un instante anterior al día devuelve 0 ("disponible desde el principio").
    /// - Un instante posterior devuelve `kMinutosPorDia` ("disponible hasta el
    ///   final"), que es el valor que usa el recorte de disponibilidades.
    /// - El límite superior es 1440, no 1439, porque representa "hasta el final
    ///   del día" como instante, no como el último minuto con un partido dentro.
    int minuto_en_dia(std::time_t instante, DiaCivil dia);

} // namespace referee_scheduler::algorithms
