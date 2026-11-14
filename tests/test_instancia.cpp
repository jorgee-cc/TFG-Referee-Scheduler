// Pruebas de la construcción de los datos estáticos, una por fase.
// Cubre de momento la fase 1 (partición por fecha civil), la fase 2
// (compactación de árbitros) y las utilidades de `tiempo.hpp` que usan: fecha
// civil local, minuto del día, índice de día y recorte de disponibilidades.
// Es el ejecutable `referee_scheduler_instancia_tests` de CTest.

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <utility>
#include <vector>

#include "referee_scheduler/algorithms/construir_instancia.hpp"
#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace
{

    using referee_scheduler::algorithms::ArbitroDia;
    using referee_scheduler::algorithms::DiaCivil;
    using referee_scheduler::algorithms::GrupoDia;
    using referee_scheduler::algorithms::agrupar_por_dia;
    using referee_scheduler::algorithms::compactar_arbitros;
    using referee_scheduler::algorithms::dia_de;
    using referee_scheduler::algorithms::indice_dia;
    using referee_scheduler::algorithms::kMinutosPorDia;
    using referee_scheduler::algorithms::minuto_absoluto;
    using referee_scheduler::algorithms::minuto_del_dia;
    using referee_scheduler::algorithms::minuto_en_dia;
    using referee_scheduler::domain::Arbitro;
    using referee_scheduler::domain::Instancia;
    using referee_scheduler::domain::Partido;

    int fallos = 0;

    /// Registra un fallo sin abortar: así se ven todos los problemas de una vez.
    void comprobar(bool condicion, const char *mensaje)
    {
        if (!condicion)
        {
            std::cerr << "FALLO: " << mensaje << '\n';
            ++fallos;
        }
    }

    /// Construye un `time_t` a partir de una fecha y hora en HORA LOCAL.
    ///
    /// Se usa `mktime` (que interpreta `tm` como hora local) porque todo el
    /// modelo trabaja con la hora que ve el usuario; `dia_de` deshace la
    /// conversión con `localtime_r`. `tm_isdst = -1` deja que la biblioteca
    /// decida el horario de verano, igual que haría con datos reales.
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

    /// Partido mínimo: solo importan el id y los instantes.
    Partido partido(int id, std::time_t inicio, std::time_t final)
    {
        Partido p;
        p.id = id;
        p.inicio = inicio;
        p.final = final;
        return p;
    }

    /// Árbitro mínimo con los campos que usa la compactación.
    Arbitro arbitro(int id, int nivel, int sede, std::time_t inicio,
                    std::time_t final, std::vector<int> vetos)
    {
        Arbitro a;
        a.id = id;
        a.nivel = nivel;
        a.sede_id = sede;
        a.disponibilidad_inicio = inicio;
        a.disponibilidad_fin = final;
        a.vetos = std::move(vetos);
        return a;
    }

    /// Comprueba que un grupo contiene exactamente `esperados`, en ese orden.
    bool contiene(const GrupoDia &grupo, const std::vector<std::size_t> &esperados)
    {
        return grupo.partidos == esperados;
    }

} // namespace

int main()
{
    // ---------------------------------------------------------------------
    // Utilidades de calendario
    // ---------------------------------------------------------------------

    // Referencia conocida del algoritmo de días civiles.
    comprobar(indice_dia(DiaCivil{1970, 1, 1}) == 0,
              "1970-01-01 debe ser el día 0");
    comprobar(indice_dia(DiaCivil{1970, 1, 2}) == 1,
              "1970-01-02 debe ser el día 1");
    comprobar(indice_dia(DiaCivil{1999, 12, 31}) == 10956,
              "1999-12-31 debe ser el día 10956");
    comprobar(indice_dia(DiaCivil{2000, 1, 1}) == 10957,
              "2000-01-01 debe ser el día 10957");
    // 2024 es bisiesto: del 28 de febrero al 1 de marzo hay dos días.
    comprobar(indice_dia(DiaCivil{2024, 3, 1}) -
                      indice_dia(DiaCivil{2024, 2, 28}) == 2,
              "2024 es bisiesto: febrero tiene 29 días");
    comprobar(indice_dia(DiaCivil{2023, 3, 1}) -
                      indice_dia(DiaCivil{2023, 2, 28}) == 1,
              "2023 no es bisiesto: febrero tiene 28 días");

    // Orden y igualdad de fechas civiles.
    comprobar(DiaCivil{2026, 11, 13} == DiaCivil{2026, 11, 13},
              "dos fechas iguales deben comparar igual");
    comprobar(!(DiaCivil{2026, 11, 13} == DiaCivil{2026, 11, 14}),
              "dos fechas distintas no deben comparar igual");
    comprobar(DiaCivil{2026, 11, 13} < DiaCivil{2026, 11, 14},
              "el orden de fechas debe ser cronológico");
    comprobar(DiaCivil{2026, 12, 1} < DiaCivil{2027, 1, 1},
              "el orden de fechas debe pasar de año");
    comprobar(!(DiaCivil{2026, 12, 1} < DiaCivil{2026, 12, 1}),
              "una fecha no es menor que sí misma");

    // Descomposición de un instante local (ida y vuelta con mktime).
    const std::time_t viernes_tarde = instante(2026, 11, 13, 18, 5);
    const DiaCivil dia_viernes = dia_de(viernes_tarde);
    comprobar(dia_viernes == DiaCivil{2026, 11, 13},
              "dia_de debe devolver la fecha local del instante");
    comprobar(minuto_del_dia(viernes_tarde) == 18 * 60 + 5,
              "minuto_del_dia debe devolver 18:05 como 1085");

    // Minutos relativos a un día, con recorte.
    comprobar(minuto_en_dia(viernes_tarde, dia_viernes) == 1085,
              "el mismo día debe dar el minuto del día");
    comprobar(minuto_en_dia(viernes_tarde, DiaCivil{2026, 11, 12}) ==
                  kMinutosPorDia,
              "un instante posterior al día debe recortarse a 1440");
    comprobar(minuto_en_dia(viernes_tarde, DiaCivil{2026, 11, 14}) == 0,
              "un instante anterior al día debe recortarse a 0");
    comprobar(minuto_absoluto(viernes_tarde) ==
                  indice_dia(dia_viernes) * kMinutosPorDia + 1085,
              "minuto_absoluto debe componer índice de día y minuto");

    // Un día completo cabe en los 11 bits del estado.
    comprobar(kMinutosPorDia <= 2047,
              "1440 minutos deben caber en 11 bits");

    // ---------------------------------------------------------------------
    // Fase 1: partición por día
    // ---------------------------------------------------------------------

    Instancia vacia;
    comprobar(agrupar_por_dia(vacia).empty(),
              "una instancia sin partidos no genera grupos");

    Instancia instancia;
    // Viernes 13: dos partidos de tarde y uno nocturno que termina el sábado.
    instancia.partidos.push_back(partido(
        0, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 19, 30)));
    instancia.partidos.push_back(partido(
        1, instante(2026, 11, 13, 20, 0), instante(2026, 11, 13, 21, 30)));
    // Sábado 14 al mediodía.
    instancia.partidos.push_back(partido(
        2, instante(2026, 11, 14, 12, 0), instante(2026, 11, 14, 13, 30)));
    // Viernes 13 a las 23:30, termina el sábado a la 01:00.
    instancia.partidos.push_back(partido(
        3, instante(2026, 11, 13, 23, 30), instante(2026, 11, 14, 1, 0)));
    // Domingo 15 por la mañana.
    instancia.partidos.push_back(partido(
        4, instante(2026, 11, 15, 11, 0), instante(2026, 11, 15, 12, 30)));

    const std::vector<GrupoDia> grupos = agrupar_por_dia(instancia);
    comprobar(grupos.size() == 3, "deben salir tres jornadas");

    if (grupos.size() == 3)
    {
        comprobar(grupos[0].dia == DiaCivil{2026, 11, 13},
                  "el primer grupo debe ser el viernes");
        comprobar(grupos[1].dia == DiaCivil{2026, 11, 14},
                  "el segundo grupo debe ser el sábado");
        comprobar(grupos[2].dia == DiaCivil{2026, 11, 15},
                  "el tercer grupo debe ser el domingo");

        // El partido 3 termina el sábado pero empieza el viernes: va al viernes.
        comprobar(contiene(grupos[0], {0, 1, 3}),
                  "el partido nocturno pertenece al día en que empieza");
        comprobar(contiene(grupos[1], {2}),
                  "el sábado solo debe tener su partido");
        comprobar(contiene(grupos[2], {4}),
                  "el domingo solo debe tener su partido");
    }

    // Un único partido genera un único grupo.
    Instancia una_jornada;
    una_jornada.partidos.push_back(partido(
        0, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 10, 0)));
    const std::vector<GrupoDia> un_grupo = agrupar_por_dia(una_jornada);
    comprobar(un_grupo.size() == 1, "un solo partido debe dar un solo grupo");
    if (!un_grupo.empty())
    {
        comprobar(un_grupo[0].dia == DiaCivil{2026, 11, 13},
                  "el grupo debe llevar la fecha del partido");
        comprobar(contiene(un_grupo[0], {0}),
                  "el grupo debe contener el partido");
    }

    // Los partidos del mismo día conservan el orden de entrada aunque la
    // entrada esté desordenada en el tiempo.
    Instancia desordenada;
    desordenada.partidos.push_back(partido(
        0, instante(2026, 11, 13, 20, 0), instante(2026, 11, 13, 21, 0)));
    desordenada.partidos.push_back(partido(
        1, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 10, 0)));
    const std::vector<GrupoDia> grupo_desordenado = agrupar_por_dia(desordenada);
    comprobar(grupo_desordenado.size() == 1,
              "los dos partidos son del mismo día");
    if (!grupo_desordenado.empty())
    {
        comprobar(contiene(grupo_desordenado[0], {0, 1}),
                  "dentro del día se respeta el orden de entrada");
    }

    // ---------------------------------------------------------------------
    // Fase 2: compactación de árbitros
    // ---------------------------------------------------------------------

    // Equipos de cada partido del fin de semana (para probar el filtrado de
    // vetos). Viernes: 100/101, 102/103 y 108/109; sábado: 104/105; domingo:
    // 106/107.
    instancia.partidos[0].equipo_local_id = 100;
    instancia.partidos[0].equipo_visitante_id = 101;
    instancia.partidos[1].equipo_local_id = 102;
    instancia.partidos[1].equipo_visitante_id = 103;
    instancia.partidos[3].equipo_local_id = 108;
    instancia.partidos[3].equipo_visitante_id = 109;
    instancia.partidos[2].equipo_local_id = 104;
    instancia.partidos[2].equipo_visitante_id = 105;
    instancia.partidos[4].equipo_local_id = 106;
    instancia.partidos[4].equipo_visitante_id = 107;

    // Se meten a propósito desordenados y con ids no consecutivos: el resultado
    // debe salir ordenado por id global y los ids compactos deben ser 0..N-1.
    instancia.arbitros.push_back(arbitro(
        30, 2, 1, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 23, 0),
        {100, 999, 100}));
    instancia.arbitros.push_back(arbitro(
        10, 1, 2, instante(2026, 11, 14, 9, 0), instante(2026, 11, 14, 23, 0),
        {}));
    // Empieza el jueves: solo puede coger los primeros partidos del viernes.
    instancia.arbitros.push_back(arbitro(
        50, 3, 1, instante(2026, 11, 12, 18, 0), instante(2026, 11, 13, 1, 0),
        {}));
    // De viernes 23:00 a sábado 23:00: entra en los dos días.
    instancia.arbitros.push_back(arbitro(
        20, 2, 2, instante(2026, 11, 13, 23, 0), instante(2026, 11, 14, 23, 0),
        {}));
    instancia.arbitros.push_back(arbitro(
        40, 1, 2, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 18, 0),
        {}));
    // Ventana invertida: dato inválido, no debe entrar en ningún día.
    instancia.arbitros.push_back(arbitro(
        60, 1, 2, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 9, 0),
        {}));
    // Solo el miércoles anterior: no toca ninguna jornada del fin de semana.
    instancia.arbitros.push_back(arbitro(
        70, 1, 2, instante(2026, 11, 11, 9, 0), instante(2026, 11, 11, 20, 0),
        {}));

    const std::vector<ArbitroDia> viernes = compactar_arbitros(instancia, grupos[0]);
    const std::vector<ArbitroDia> sabado = compactar_arbitros(instancia, grupos[1]);
    const std::vector<ArbitroDia> domingo = compactar_arbitros(instancia, grupos[2]);

    // Viernes: 20 (23:00-24:00), 30 (9:00-23:00), 40 (9:00-18:00) y 50 (acaba
    // a la 01:00). Fuera 10 (sábado), 60 (ventana invertida) y 70 (miércoles).
    comprobar(viernes.size() == 4, "el viernes deben quedar cuatro árbitros");
    if (viernes.size() == 4)
    {
        comprobar(viernes[0].id_global == 20, "los ids compactos van por id global (1/4)");
        comprobar(viernes[1].id_global == 30, "los ids compactos van por id global (2/4)");
        comprobar(viernes[2].id_global == 40, "los ids compactos van por id global (3/4)");
        comprobar(viernes[3].id_global == 50, "los ids compactos van por id global (4/4)");

        // Puede empezar a las 23:00 (último minuto permitido = 1380).
        comprobar(viernes[0].primer_inicio == 23 * 60,
                  "el árbitro nocturno empieza a las 23:00");
        comprobar(viernes[0].ultimo_inicio == kMinutosPorDia,
                  "su disponibilidad cubre hasta el final del día");

        // 9:00-23:00 recortado al día.
        comprobar(viernes[1].primer_inicio == 9 * 60,
                  "primer_inicio recorta el inicio de la ventana");
        comprobar(viernes[1].ultimo_inicio == 23 * 60,
                  "ultimo_inicio recorta el fin de la ventana");

        // 9:00-18:00.
        comprobar(viernes[2].primer_inicio == 9 * 60,
                  "primer_inicio de la ventana corta");
        comprobar(viernes[2].ultimo_inicio == 18 * 60,
                  "ultimo_inicio de la ventana corta");

        // Venía del jueves: el día empieza para él en el minuto 0 y acaba a la 1:00.
        comprobar(viernes[3].primer_inicio == 0,
                  "una ventana que viene del día anterior empieza en 0");
        comprobar(viernes[3].ultimo_inicio == 1 * 60,
                  "una ventana que acaba de madrugada termina en su minuto");

        // Vetos: se descarta el equipo 999 (no juega) y el duplicado 100.
        comprobar(viernes[1].equipos_vetados == std::vector<int>{100},
                  "solo sobreviven los vetos contra equipos del día, sin duplicados");
        comprobar(viernes[0].equipos_vetados.empty(),
                  "un árbitro sin vetos deja la lista vacía");
    }

    // Sábado: 10 (9:00-23:00) y 20 (viene del viernes a las 23:00).
    comprobar(sabado.size() == 2, "el sábado deben quedar dos árbitros");
    if (sabado.size() == 2)
    {
        comprobar(sabado[0].id_global == 10, "el sábado entra el árbitro del sábado");
        comprobar(sabado[1].id_global == 20, "el sábado entra el árbitro que viene del viernes");
        comprobar(sabado[0].primer_inicio == 9 * 60,
                  "el árbitro del sábado empieza a las 9:00");
        comprobar(sabado[0].ultimo_inicio == 23 * 60,
                  "el árbitro del sábado acaba a las 23:00");
        comprobar(sabado[1].primer_inicio == 0,
                  "la ventana del viernes se recorta al sábado desde el minuto 0");
        comprobar(sabado[1].ultimo_inicio == 23 * 60,
                  "su último inicio del sábado son las 23:00");
    }

    // Domingo: nadie declaró disponibilidad.
    comprobar(domingo.empty(),
              "sin disponibilidad el domingo se queda sin árbitros");

    // El id compacto es la posición: no hay huecos ni ids repetidos.
    for (std::size_t i = 0; i < viernes.size(); ++i)
    {
        comprobar(viernes[i].id_global > 0,
                  "cada árbitro compactado conserva su id global");
        if (i > 0)
        {
            comprobar(viernes[i - 1].id_global < viernes[i].id_global,
                      "los ids globales van en orden estricto");
        }
    }

    if (fallos == 0)
    {
        std::cout << "test_instancia (fases 1-2): OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
