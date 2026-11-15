// Pruebas de la construcción de los datos estáticos, una por fase.
// Cubre de momento la fase 1 (partición por fecha civil), la fase 2
// (compactación de árbitros), la fase 3 (matriz de distancias), la fase 4
// (expansión de plazas ordenadas) y las utilidades de `tiempo.hpp` que usan:
// fecha civil local, minuto del día, índice de día y recorte de
// disponibilidades. Es el ejecutable `referee_scheduler_instancia_tests` de
// CTest.

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

#include "referee_scheduler/algorithms/construir_instancia.hpp"
#include "referee_scheduler/algorithms/tiempo.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace
{

    using referee_scheduler::algorithms::ArbitroDia;
    using referee_scheduler::algorithms::Coordenada;
    using referee_scheduler::algorithms::DiaCivil;
    using referee_scheduler::algorithms::GrupoDia;
    using referee_scheduler::algorithms::InstanciaDia;
    using referee_scheduler::algorithms::Minutos;
    using referee_scheduler::algorithms::OpcionesDistancias;
    using referee_scheduler::algorithms::PlazaDia;
    using referee_scheduler::algorithms::ProveedorDistancias;
    using referee_scheduler::algorithms::agrupar_por_dia;
    using referee_scheduler::algorithms::cerrar_metricamente;
    using referee_scheduler::algorithms::compactar_arbitros;
    using referee_scheduler::algorithms::construir_instancia_dia;
    using referee_scheduler::algorithms::construir_instancias;
    using referee_scheduler::algorithms::construir_plazas;
    using referee_scheduler::algorithms::dia_de;
    using referee_scheduler::algorithms::indice_dia;
    using referee_scheduler::algorithms::kMinutosInalcanzable;
    using referee_scheduler::algorithms::kMinutosPorDia;
    using referee_scheduler::algorithms::minuto_absoluto;
    using referee_scheduler::algorithms::minuto_del_dia;
    using referee_scheduler::algorithms::minuto_en_dia;
    using referee_scheduler::domain::Arbitro;
    using referee_scheduler::domain::Instancia;
    using referee_scheduler::domain::Partido;
    using referee_scheduler::domain::Plaza;
    using referee_scheduler::domain::Rol;
    using referee_scheduler::domain::Sede;

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

    /// Plaza mínima con los campos que usa la expansión de la fase 4.
    Plaza plaza(int id, int partido_id, Rol rol, int nivel, double retribucion)
    {
        Plaza p;
        p.id = id;
        p.partido_id = partido_id;
        p.rol = rol;
        p.nivel_requerido = nivel;
        p.retribucion = retribucion;
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

    /// Sede geolocalizada de forma determinista: el id hace de latitud y su
    /// negativo de longitud, así se puede comprobar el orden de las coordenadas.
    Sede sede(int id)
    {
        Sede s;
        s.id = id;
        s.latitud = static_cast<double>(id);
        s.longitud = -static_cast<double>(id);
        return s;
    }

    /// Proveedor de distancias que devuelve siempre la matriz guionizada.
    ///
    /// Guarda las coordenadas recibidas para poder comprobar que la
    /// compactación las pasó en orden ascendente de id.
    class ProveedorGuionizado : public ProveedorDistancias
    {
    public:
        explicit ProveedorGuionizado(std::vector<Minutos> valores)
            : valores_(std::move(valores))
        {
        }

        std::vector<Minutos>
        matriz_minutos(std::span<const Coordenada> coordenadas) override
        {
            recibidas.assign(coordenadas.begin(), coordenadas.end());
            return valores_;
        }

        /// Coordenadas que recibió en la última llamada.
        std::vector<Coordenada> recibidas;

    private:
        std::vector<Minutos> valores_;
    };

    /// Proveedor que devuelve la misma distancia en todas las parejas.
    ///
    /// Sirve para construir varios días con tamaños de matriz distintos sin
    /// tener que preparar una matriz por día.
    class ProveedorConstante : public ProveedorDistancias
    {
    public:
        explicit ProveedorConstante(Minutos valor) : valor_(valor) {}

        std::vector<Minutos>
        matriz_minutos(std::span<const Coordenada> coordenadas) override
        {
            return std::vector<Minutos>(coordenadas.size() * coordenadas.size(),
                                        valor_);
        }

    private:
        Minutos valor_;
    };

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
    const DiaCivil fecha_viernes = dia_de(viernes_tarde);
    comprobar(fecha_viernes == DiaCivil{2026, 11, 13},
              "dia_de debe devolver la fecha local del instante");
    comprobar(minuto_del_dia(viernes_tarde) == 18 * 60 + 5,
              "minuto_del_dia debe devolver 18:05 como 1085");

    // Minutos relativos a un día, con recorte.
    comprobar(minuto_en_dia(viernes_tarde, fecha_viernes) == 1085,
              "el mismo día debe dar el minuto del día");
    comprobar(minuto_en_dia(viernes_tarde, DiaCivil{2026, 11, 12}) ==
                  kMinutosPorDia,
              "un instante posterior al día debe recortarse a 1440");
    comprobar(minuto_en_dia(viernes_tarde, DiaCivil{2026, 11, 14}) == 0,
              "un instante anterior al día debe recortarse a 0");
    comprobar(minuto_absoluto(viernes_tarde) ==
                  indice_dia(fecha_viernes) * kMinutosPorDia + 1085,
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

    // Sedes de los partidos: dos el viernes, una el sábado y una el domingo.
    instancia.partidos[0].sede_id = 10;
    instancia.partidos[1].sede_id = 11;
    instancia.partidos[3].sede_id = 12;
    instancia.partidos[2].sede_id = 20;
    instancia.partidos[4].sede_id = 21;

    // Las sedes se meten desordenadas a propósito: la fase 3 debe compactarlas
    // por id ascendente (10, 11, 12, 13, 20, 21).
    instancia.sedes = {sede(13), sede(10), sede(21),
                       sede(11), sede(20), sede(12)};

    // Plazas del fin de semana. Se meten desordenadas y con ids que no coinciden
    // con la posición en el vector: así se comprueba que el orden final lo fija
    // el criterio de la fase 4 y no el orden de entrada. La retribución 30,50 €
    // sirve para verificar la conversión a céntimos enteros (3050).
    //   - partido 0 (viernes 18:00, sede 10): plazas 500 y 501.
    //   - partido 1 (viernes 20:00, sede 11): plaza 502.
    //   - partido 3 (viernes 23:30, sede 12): plaza 503.
    //   - partido 2 (sábado 12:00, sede 20): plaza 504.
    //   - partido 4 (domingo 11:00, sede 21): plaza 505.
    instancia.plazas.push_back(plaza(501, 0, Rol::Principal, 2, 30.50));
    instancia.plazas.push_back(plaza(503, 3, Rol::Principal, 2, 40.00));
    instancia.plazas.push_back(plaza(505, 4, Rol::Principal, 3, 25.00));
    instancia.plazas.push_back(plaza(500, 0, Rol::Asistente, 1, 12.00));
    instancia.plazas.push_back(plaza(504, 2, Rol::JuezLinea, 1, 10.00));
    instancia.plazas.push_back(plaza(502, 1, Rol::Asistente, 1, 12.00));

    // Se meten a propósito desordenados y con ids no consecutivos: el resultado
    // debe salir ordenado por id global y los ids compactos deben ser 0..N-1.
    instancia.arbitros.push_back(arbitro(
        30, 2, 10, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 23, 0),
        {100, 999, 100}));
    instancia.arbitros.push_back(arbitro(
        10, 1, 11, instante(2026, 11, 14, 9, 0), instante(2026, 11, 14, 23, 0),
        {}));
    // Empieza el jueves: solo puede coger los primeros partidos del viernes.
    instancia.arbitros.push_back(arbitro(
        50, 3, 12, instante(2026, 11, 12, 18, 0), instante(2026, 11, 13, 1, 0),
        {}));
    // De viernes 23:00 a sábado 23:00: entra en los dos días.
    instancia.arbitros.push_back(arbitro(
        20, 2, 13, instante(2026, 11, 13, 23, 0), instante(2026, 11, 14, 23, 0),
        {}));
    instancia.arbitros.push_back(arbitro(
        40, 1, 10, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 18, 0),
        {}));
    // Ventana invertida: dato inválido, no debe entrar en ningún día.
    instancia.arbitros.push_back(arbitro(
        60, 1, 10, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 9, 0),
        {}));
    // Solo el miércoles anterior: no toca ninguna jornada del fin de semana.
    instancia.arbitros.push_back(arbitro(
        70, 1, 10, instante(2026, 11, 11, 9, 0), instante(2026, 11, 11, 20, 0),
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

    // ---------------------------------------------------------------------
    // Fase 3: matriz de distancias
    // ---------------------------------------------------------------------

    const OpcionesDistancias con_cierre; // cerrar_metricamente = true
    OpcionesDistancias sin_cierre;
    sin_cierre.cerrar_metricamente = false;

    // Matriz asimétrica y con la diagonal "mala" a propósito. El camino
    // 0->3->2 (5 + 12) incumple la desigualdad triangular frente a 0->2 (100).
    ProveedorGuionizado proveedor_viernes({
        999, 10, 100, 5,
        12, 999, 10, 6,
        7, 8, 999, 50,
        9, 11, 12, 999});

    const InstanciaDia dia_viernes = construir_instancia_dia(
        instancia, grupos[0], proveedor_viernes, con_cierre);

    // Sedes del viernes: las de los partidos (10, 11, 12) más el domicilio 13.
    comprobar(dia_viernes.n_sedes() == 4,
              "el viernes debe compactar cuatro sedes");
    comprobar(dia_viernes.sede_global == std::vector<int>{10, 11, 12, 13},
              "las sedes se compactan por id ascendente");
    comprobar(proveedor_viernes.recibidas.size() == 4,
              "el proveedor debe recibir una coordenada por sede");
    if (proveedor_viernes.recibidas.size() == 4)
    {
        comprobar(proveedor_viernes.recibidas[0].latitud == 10.0,
                  "la primera coordenada es la sede de menor id");
        comprobar(proveedor_viernes.recibidas[3].latitud == 13.0,
                  "la última coordenada es la sede de mayor id");
        comprobar(proveedor_viernes.recibidas[0].longitud == -10.0,
                  "la longitud se pasa tal cual");
    }

    // Diagonal forzada a 0 aunque el proveedor devuelva otra cosa.
    comprobar(dia_viernes.viaje(0, 0) == 0, "la diagonal debe ser cero (1)");
    comprobar(dia_viernes.viaje(1, 1) == 0, "la diagonal debe ser cero (2)");
    comprobar(dia_viernes.viaje(2, 2) == 0, "la diagonal debe ser cero (3)");
    comprobar(dia_viernes.viaje(3, 3) == 0, "la diagonal debe ser cero (4)");

    // La matriz no se espeja: cada sentido tiene su tiempo.
    comprobar(dia_viernes.viaje(0, 1) == 10, "tiempo directo 0->1");
    comprobar(dia_viernes.viaje(1, 0) == 12, "tiempo directo 1->0");
    comprobar(dia_viernes.viaje(2, 0) == 7, "tiempo directo 2->0");

    // Con cierre, 0->2 baja a 5 + 12 = 17 pasando por la sede 3.
    comprobar(dia_viernes.viaje(0, 2) == 17,
              "el cierre metrico debe encontrar el camino 0->3->2");

    // Sin cierre, la celda se queda como la dio el proveedor.
    ProveedorGuionizado proveedor_sin_cierre({
        999, 10, 100, 5,
        12, 999, 10, 6,
        7, 8, 999, 50,
        9, 11, 12, 999});
    const InstanciaDia dia_sin_cierre = construir_instancia_dia(
        instancia, grupos[0], proveedor_sin_cierre, sin_cierre);
    comprobar(dia_sin_cierre.viaje(0, 2) == 100,
              "sin cierre la matriz es exactamente la del proveedor");
    comprobar(dia_sin_cierre.viaje(1, 0) == 12,
              "sin cierre tampoco se espeja la matriz");

    // Domicilios compactados: árbitros [20, 30, 40, 50] viven en las sedes
    // [13, 10, 10, 12], que son los índices [3, 0, 0, 2].
    comprobar(dia_viernes.n_arbitros() == 4,
              "el viernes conserva sus cuatro árbitros");
    if (dia_viernes.n_arbitros() == 4)
    {
        comprobar(dia_viernes.arbitros[0].sede_domicilio == 3,
                  "domicilio compacto del árbitro 20");
        comprobar(dia_viernes.arbitros[1].sede_domicilio == 0,
                  "domicilio compacto del árbitro 30");
        comprobar(dia_viernes.arbitros[2].sede_domicilio == 0,
                  "domicilio compacto del árbitro 40");
        comprobar(dia_viernes.arbitros[3].sede_domicilio == 2,
                  "domicilio compacto del árbitro 50");
    }

    // Una pareja sin ruta sigue siendo inalcanzable después del cierre.
    {
        Instancia aislada;
        aislada.partidos.push_back(partido(
            0, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 19, 0)));
        aislada.partidos[0].sede_id = 1;
        aislada.arbitros.push_back(arbitro(
            1, 1, 2, instante(2026, 11, 13, 9, 0), instante(2026, 11, 13, 20, 0),
            {}));
        aislada.sedes = {sede(1), sede(2)};

        ProveedorGuionizado inalcanzable(
            {0, kMinutosInalcanzable, kMinutosInalcanzable, 0});
        const std::vector<GrupoDia> grupos_aislados = agrupar_por_dia(aislada);

        const InstanciaDia dia_aislado = construir_instancia_dia(
            aislada, grupos_aislados[0], inalcanzable, con_cierre);
        comprobar(dia_aislado.viaje(0, 1) == kMinutosInalcanzable,
                  "sin ruta de ida debe quedar inalcanzable");
        comprobar(dia_aislado.viaje(1, 0) == kMinutosInalcanzable,
                  "sin ruta de vuelta debe quedar inalcanzable");
    }

    // Cierre métrico aislado: usa el puente 0->1->2 y no inventa la vuelta.
    {
        std::vector<Minutos> puente = {
            0, 10, kMinutosInalcanzable,
            kMinutosInalcanzable, 0, 10,
            kMinutosInalcanzable, kMinutosInalcanzable, 0};
        cerrar_metricamente(puente, 3);

        comprobar(puente[0 * 3 + 2] == 20,
                  "el cierre debe encontrar el camino 0->1->2");
        comprobar(puente[1 * 3 + 0] == kMinutosInalcanzable,
                  "el cierre no debe inventar el camino inverso");
        comprobar(puente[0] == 0 && puente[4] == 0 && puente[8] == 0,
                  "el cierre no debe tocar la diagonal");
    }

    // ---------------------------------------------------------------------
    // Fase 4: expansión y orden de las plazas
    // ---------------------------------------------------------------------

    // `dia_viernes` ya se construyó con las fases 1-4, así que trae sus plazas.
    // De las seis plazas de la instancia solo cuatro son del viernes.
    comprobar(dia_viernes.n_plazas() == 4,
              "el viernes debe tener cuatro plazas");
    if (dia_viernes.n_plazas() == 4)
    {
        // Orden (minuto_inicio, sede, partido_id, plaza_id):
        // 18:00 -> 500 y 501 (mismo partido y sede, desempata plaza_id),
        // 20:00 -> 502, 23:30 -> 503.
        comprobar(dia_viernes.plazas[0].plaza_id == 500,
                  "la primera plaza del viernes es la 500");
        comprobar(dia_viernes.plazas[1].plaza_id == 501,
                  "a igualdad de partido y sede desempata el id de plaza");
        comprobar(dia_viernes.plazas[2].plaza_id == 502,
                  "después va la plaza de las 20:00");
        comprobar(dia_viernes.plazas[3].plaza_id == 503,
                  "la última es la del partido nocturno");

        // Partido 0: 18:00-19:30, sede global 10 -> índice compacto 0.
        const PlazaDia &primera = dia_viernes.plazas[0];
        comprobar(primera.minuto_inicio == 18 * 60,
                  "minuto_inicio del partido de las 18:00");
        comprobar(primera.minuto_fin == 19 * 60 + 30,
                  "minuto_fin del partido de las 18:00");
        comprobar(primera.sede == 0, "la sede 10 se compacta en el índice 0");
        comprobar(primera.partido_id == 0, "la plaza conserva su partido");
        comprobar(primera.nivel_requerido == 1,
                  "la plaza de asistente exige nivel 1");
        comprobar(primera.retribucion_centimos == 1200,
                  "12,00 euros son 1200 centimos");
        comprobar(primera.rol == Rol::Asistente,
                  "la plaza 500 es de asistente");
        comprobar(!primera.es_principal(),
                  "una plaza de asistente no es principal");
        comprobar(primera.equipo_local_id == 100 &&
                      primera.equipo_visitante_id == 101,
                  "la plaza lleva los dos equipos del partido");

        const PlazaDia &segunda = dia_viernes.plazas[1];
        comprobar(segunda.retribucion_centimos == 3050,
                  "30,50 euros son 3050 centimos (redondeo a centimo)");
        comprobar(segunda.es_principal(),
                  "la plaza 501 es principal y no puede quedarse vacia");

        // Partido 1: 20:00-21:30 en la sede global 11 -> índice 1.
        comprobar(dia_viernes.plazas[2].minuto_inicio == 20 * 60,
                  "minuto_inicio del partido de las 20:00");
        comprobar(dia_viernes.plazas[2].minuto_fin == 21 * 60 + 30,
                  "minuto_fin del partido de las 20:00");
        comprobar(dia_viernes.plazas[2].sede == 1,
                  "la sede 11 se compacta en el indice 1");

        // Partido 3: 23:30 del viernes a 01:00 del sabado. El fin se mide
        // desde las 00:00 del viernes, asi que vale 1500 y no se recorta.
        const PlazaDia &nocturna = dia_viernes.plazas[3];
        comprobar(nocturna.minuto_inicio == 23 * 60 + 30,
                  "el partido nocturno empieza a las 23:30");
        comprobar(nocturna.minuto_fin == kMinutosPorDia + 60,
                  "un partido que acaba a la 01:00 termina en el minuto 1500");
        comprobar(nocturna.sede == 2,
                  "la sede 12 se compacta en el indice 2");
        comprobar(nocturna.retribucion_centimos == 4000,
                  "40,00 euros son 4000 centimos");
    }

    // El sábado la sede 20 es la tercera compacta (11, 13, 20): el registro
    // guarda el índice de la matriz, no el id global.
    {
        ProveedorConstante constante(7);
        const std::vector<InstanciaDia> dias =
            construir_instancias(instancia, constante);

        comprobar(dias.size() == 3, "deben construirse las tres jornadas");
        if (dias.size() == 3)
        {
            // El sábado solo tiene la plaza 504: 12:00-13:30 en la sede 20.
            comprobar(dias[1].n_plazas() == 1,
                      "el sabado solo tiene una plaza");
            if (dias[1].n_plazas() == 1)
            {
                const PlazaDia &sabado = dias[1].plazas[0];
                comprobar(sabado.plaza_id == 504, "la plaza del sabado es la 504");
                comprobar(sabado.minuto_inicio == 12 * 60,
                          "el partido del sabado empieza a las 12:00");
                comprobar(sabado.minuto_fin == 13 * 60 + 30,
                          "y acaba a las 13:30");
                comprobar(sabado.sede == 2,
                          "la sede 20 es la tercera compacta del sabado");
                comprobar(sabado.rol == Rol::JuezLinea,
                          "la plaza del sabado es de juez de linea");
                comprobar(!sabado.es_principal(),
                          "un juez de linea no es principal");
                comprobar(sabado.retribucion_centimos == 1000,
                          "10,00 euros son 1000 centimos");
                comprobar(sabado.partido_id == 2 &&
                              sabado.equipo_local_id == 104 &&
                              sabado.equipo_visitante_id == 105,
                          "la plaza lleva el partido y sus dos equipos");
            }

            // El domingo no tiene árbitros, pero sí su plaza.
            comprobar(dias[2].n_plazas() == 1,
                      "el domingo conserva su plaza aunque no haya arbitros");
            if (dias[2].n_plazas() == 1)
            {
                comprobar(dias[2].plazas[0].minuto_inicio == 11 * 60 &&
                              dias[2].plazas[0].minuto_fin == 12 * 60 + 30,
                          "horario de la plaza del domingo");
                comprobar(dias[2].plazas[0].nivel_requerido == 3,
                          "la plaza del domingo exige nivel 3");
            }
        }
    }

    // Desempates del orden: dos partidos a la misma hora en sedes distintas y
    // dos plazas del mismo partido. Se llama a la fase 4 directamente porque
    // aquí no hacen falta árbitros ni matriz.
    {
        Instancia empates;
        empates.partidos.push_back(partido(
            7, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 19, 30)));
        empates.partidos.push_back(partido(
            8, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 19, 30)));
        empates.partidos.push_back(partido(
            9, instante(2026, 11, 13, 18, 0), instante(2026, 11, 13, 19, 30)));
        empates.partidos[0].sede_id = 31;
        empates.partidos[1].sede_id = 30;
        empates.partidos[2].sede_id = 30;

        empates.plazas.push_back(plaza(703, 7, Rol::Asistente, 1, 10.0));
        empates.plazas.push_back(plaza(702, 7, Rol::Asistente, 1, 10.0));
        empates.plazas.push_back(plaza(701, 8, Rol::Principal, 1, 10.0));
        empates.plazas.push_back(plaza(704, 9, Rol::Asistente, 1, 10.0));
        empates.plazas.push_back(plaza(700, 9, Rol::Principal, 1, 10.0));

        const std::vector<GrupoDia> grupos_empates = agrupar_por_dia(empates);
        const std::vector<int> sedes_empates = {30, 31};
        const std::vector<PlazaDia> plazas_empates =
            construir_plazas(empates, grupos_empates[0], sedes_empates);

        // Todas empiezan a la misma hora: ordena la sede compacta (30 -> 0,
        // 31 -> 1), luego el partido y por último la plaza.
        comprobar(plazas_empates.size() == 5,
                  "deben expandirse las cinco plazas");
        if (plazas_empates.size() == 5)
        {
            const std::vector<int> esperado = {701, 700, 704, 702, 703};
            bool mismo_orden = true;
            for (std::size_t i = 0; i < esperado.size(); ++i)
            {
                if (plazas_empates[i].plaza_id != esperado[i])
                {
                    mismo_orden = false;
                }
            }
            comprobar(mismo_orden,
                      "el orden total es (inicio, sede, partido, plaza)");

            comprobar(plazas_empates[0].sede == 0 &&
                          plazas_empates[3].sede == 1,
                      "la sede compacta ordena igual que el id global");
        }

        // Sin partidos en el grupo no hay plazas que expandir.
        const std::vector<PlazaDia> sin_plazas =
            construir_plazas(empates, GrupoDia{}, sedes_empates);
        comprobar(sin_plazas.empty(),
                  "un grupo sin partidos no genera plazas");
    }

    // Pipeline completo de las fases 1-4 para las tres jornadas.
    {
        ProveedorConstante constante(7);
        const std::vector<InstanciaDia> dias =
            construir_instancias(instancia, constante);

        comprobar(dias.size() == 3, "deben construirse las tres jornadas");
        if (dias.size() == 3)
        {
            comprobar(dias[0].dia == DiaCivil{2026, 11, 13},
                      "la primera jornada es el viernes");
            comprobar(dias[1].dia == DiaCivil{2026, 11, 14},
                      "la segunda jornada es el sábado");
            comprobar(dias[2].dia == DiaCivil{2026, 11, 15},
                      "la tercera jornada es el domingo");

            // Sedes: viernes 10/11/12 + domicilio 13; sábado 20 + domicilios
            // 11/13; domingo solo la sede 21 (sin árbitros).
            comprobar(dias[0].n_sedes() == 4, "sedes del viernes");
            comprobar(dias[1].n_sedes() == 3, "sedes del sábado");
            comprobar(dias[2].n_sedes() == 1, "sedes del domingo");
            comprobar(dias[2].n_arbitros() == 0,
                      "el domingo no tiene árbitros disponibles");

            comprobar(dias[0].viaje(0, 1) == 7,
                      "la matriz constante se copia a cada día");
            comprobar(dias[0].viaje(0, 0) == 0,
                      "la diagonal sigue a cero con el proveedor constante");
        }
    }

    if (fallos == 0)
    {
        std::cout << "test_instancia (fases 1-4): OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
