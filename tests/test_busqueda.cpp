// Pruebas de integración del paso 4/6: `buscar_dijkstra` sobre instancias
// pequeñas con proveedores de distancia fijos.
//
// Se comprueba el coste exacto de casos construidos a mano, la causa de parada
// (óptima, sin solución y presupuesto agotado), el día trivial sin plazas, el
// colapso de estados duplicados (la razón de ser de `TablaEstados`) contra un
// recorrido exhaustivo sin deduplicación y que los cubos de anchura mayor dan el
// mismo óptimo.
// Es el ejecutable `referee_scheduler_busqueda_tests` de CTest.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "referee_scheduler/algorithms/busqueda.hpp"
#include "referee_scheduler/algorithms/construir_instancia.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace
{

    using referee_scheduler::algorithms::Arena;
    using referee_scheduler::algorithms::CausaParada;
    using referee_scheduler::algorithms::GrupoDia;
    using referee_scheduler::algorithms::InstanciaDia;
    using referee_scheduler::algorithms::Minutos;
    using referee_scheduler::algorithms::OpcionesBusqueda;
    using referee_scheduler::algorithms::ParametrosObjetivo;
    using referee_scheduler::algorithms::ProveedorDistancias;
    using referee_scheduler::algorithms::ResultadoBusqueda;
    using referee_scheduler::algorithms::agrupar_por_dia;
    using referee_scheduler::algorithms::buscar_dijkstra;
    using referee_scheduler::algorithms::construir_instancia_dia;
    using referee_scheduler::algorithms::crear_raiz;
    using referee_scheduler::algorithms::es_meta;
    using referee_scheduler::algorithms::generar_hijos;
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

    /// Partido mínimo con sede, horario y equipos.
    Partido partido(int id, std::time_t inicio, std::time_t final, int sede_id,
                    int local = 100, int visitante = 101)
    {
        Partido p;
        p.id = id;
        p.sede_id = sede_id;
        p.inicio = inicio;
        p.final = final;
        p.equipo_local_id = local;
        p.equipo_visitante_id = visitante;
        return p;
    }

    /// Plaza mínima con los campos que usa la búsqueda.
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

    /// Árbitro mínimo con su domicilio y su ventana de disponibilidad.
    Arbitro arbitro(int id, int nivel, int domicilio, std::time_t inicio,
                    std::time_t final)
    {
        Arbitro a;
        a.id = id;
        a.nivel = nivel;
        a.domicilio_id = domicilio;
        a.disponibilidad_inicio = inicio;
        a.disponibilidad_fin = final;
        return a;
    }

    /// Sede geolocalizada de forma determinista (las coordenadas no se usan con
    /// los proveedores fijos, pero la fase 3 exige que no sean (0, 0)).
    Sede sede(int id)
    {
        Sede s;
        s.id = id;
        s.latitud = static_cast<double>(id);
        s.longitud = -static_cast<double>(id);
        return s;
    }

    /// Proveedor que devuelve exactamente la matriz indicada.
    class ProveedorFijo : public ProveedorDistancias
    {
    public:
        explicit ProveedorFijo(std::vector<Minutos> valores)
            : valores_(std::move(valores))
        {
        }

        std::vector<Minutos>
        matriz_minutos(std::span<const referee_scheduler::algorithms::Coordenada>)
            override
        {
            return valores_;
        }

    private:
        std::vector<Minutos> valores_;
    };

    /// Proveedor que devuelve el mismo valor en todas las parejas.
    class ProveedorConstante : public ProveedorDistancias
    {
    public:
        explicit ProveedorConstante(Minutos valor) : valor_(valor) {}

        std::vector<Minutos>
        matriz_minutos(std::span<const referee_scheduler::algorithms::Coordenada>
                           coordenadas) override
        {
            return std::vector<Minutos>(coordenadas.size() * coordenadas.size(),
                                        valor_);
        }

    private:
        Minutos valor_;
    };

    /// Construye la instancia del único día de la entrada de prueba.
    InstanciaDia construir_dia(const Instancia &instancia,
                               ProveedorDistancias &proveedor)
    {
        const std::vector<GrupoDia> grupos = agrupar_por_dia(instancia);
        if (grupos.empty())
        {
            std::cerr << "FALLO: la instancia de prueba no tiene ningun dia\n";
            ++fallos;
            return InstanciaDia{};
        }
        return construir_instancia_dia(instancia, grupos.front(), proveedor);
    }

    /// Recorrido exhaustivo del árbol (sin tabla ni frontera) que devuelve el
    /// coste de la mejor meta. Es el oráculo contra el que se valida Dijkstra:
    /// visita todos los caminos, así que no puede equivocarse por deduplicar.
    std::int64_t coste_minimo_exhaustivo(Arena &arena, const InstanciaDia &dia,
                                         std::uint32_t nodo)
    {
        if (es_meta(arena, dia, nodo))
        {
            return arena.cabecera(nodo).coste_g;
        }

        // Un vector por llamada: la recursión no puede compartir el búfer.
        std::vector<std::uint32_t> hijos;
        generar_hijos(arena, dia, nodo, ParametrosObjetivo{}, hijos);

        std::int64_t mejor = std::numeric_limits<std::int64_t>::max();
        for (const std::uint32_t hijo : hijos)
        {
            mejor = std::min(mejor, coste_minimo_exhaustivo(arena, dia, hijo));
        }
        return mejor;
    }

    // ---------------------------------------------------------------------
    // Dos partidos, un árbitro: el caso ya conocido de 982 céntimos
    // ---------------------------------------------------------------------
    void probar_dos_partidos()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 10, 0),
                    instante(2026, 11, 13, 11, 0), 10));
        instancia.partidos.push_back(
            partido(1, instante(2026, 11, 13, 12, 0),
                    instante(2026, 11, 13, 13, 0), 11));
        instancia.plazas.push_back(plaza(500, 0, Rol::Principal, 1, 20.0));
        instancia.plazas.push_back(plaza(501, 1, Rol::Principal, 1, 25.0));
        instancia.arbitros.push_back(arbitro(
            7, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(11), sede(20)};

        // Todos los viajes cuestan 5 minutos.
        ProveedorFijo proveedor({
            0, 5, 5,
            5, 0, 5,
            5, 5, 0,
        });
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        Arena arena(dia.n_arbitros());
        const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia);

        comprobar(resultado.causa == CausaParada::Optima,
                  "el dia de dos partidos tiene solucion optima");
        comprobar(resultado.hay_solucion, "debe haber solucion");
        comprobar(resultado.coste == 982, "el optimo son 982 centimos");
        comprobar(resultado.solucion.has_value(), "la solucion debe reconstruirse");

        if (resultado.solucion)
        {
            comprobar(resultado.solucion->coste == resultado.coste,
                      "el coste reconstruido coincide con el de la cabecera");
            comprobar(resultado.solucion->decisiones.size() == 2,
                      "una decision por plaza");

            if (resultado.solucion->decisiones.size() == 2)
            {
                comprobar(resultado.solucion->decisiones[0].plaza_id == 500 &&
                              resultado.solucion->decisiones[1].plaza_id == 501,
                          "las decisiones van en orden cronologico");
                comprobar(resultado.solucion->decisiones[0].arbitro_global == 7 &&
                              resultado.solucion->decisiones[1].arbitro_global == 7,
                          "el unico arbitro cubre las dos plazas");
            }
        }

        // Métricas de coherencia: la raíz más un hijo por nivel, y nunca más
        // estados distintos que nodos creados.
        comprobar(resultado.nodos_creados >= 3, "deben crearse al menos raiz y dos hijos");
        comprobar(resultado.nodos_expandidos > 0, "debe expandirse algun nodo");
        comprobar(resultado.estados_distintos <= resultado.nodos_creados,
                  "no puede haber mas estados distintos que nodos");
        comprobar(resultado.nodos_expandidos <= resultado.estados_distintos,
                  "con cubos exactos cada estado se expande como mucho una vez");
        comprobar(resultado.milisegundos >= 0.0, "el tiempo medido no es negativo");
    }

    // ---------------------------------------------------------------------
    // Plaza secundaria que sale más barata vacía que asignada
    // ---------------------------------------------------------------------
    void probar_secundaria_vacia()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.plazas.push_back(plaza(600, 0, Rol::Asistente, 1, 12.0));
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(20)};

        // 200 minutos de viaje en cada sentido: asignar cuesta
        // 1 + 200*14 + 200*14 = 5601 céntimos, muy por encima de los 1200 de
        // retribución que se pierden dejándola vacía.
        ProveedorFijo proveedor({0, 200, 200, 0});
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        Arena arena(dia.n_arbitros());
        const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia);

        comprobar(resultado.causa == CausaParada::Optima, "el dia tiene solucion");
        comprobar(resultado.coste == 1200,
                  "dejar la secundaria vacia cuesta su retribucion (1200)");
        if (resultado.solucion && !resultado.solucion->decisiones.empty())
        {
            comprobar(resultado.solucion->decisiones[0].arbitro_global == -1,
                      "la plaza vacia no tiene arbitro asignado");
        }
        else
        {
            comprobar(false, "la solucion debe tener una decision");
        }
    }

    // ---------------------------------------------------------------------
    // Día sin solución: una principal que nadie puede cubrir
    // ---------------------------------------------------------------------
    void probar_sin_solucion()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.plazas.push_back(plaza(700, 0, Rol::Principal, 1, 20.0));
        // Disponible de 9:00 a 10:00: pertenece al día, pero su ventana no
        // contiene la hora del partido, así que no es candidato.
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 10, 0)));
        instancia.sedes = {sede(10), sede(20)};

        ProveedorConstante proveedor(5);
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_arbitros() == 1, "el arbitro pertenece al dia");
        comprobar(dia.n_candidatos(0) == 0, "pero no es candidato de la principal");

        Arena arena(dia.n_arbitros());
        const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia);

        comprobar(resultado.causa == CausaParada::SinSolucion,
                  "una principal sin candidatos deja el dia sin solucion");
        comprobar(!resultado.hay_solucion, "no debe haber solucion");
        comprobar(!resultado.solucion.has_value(), "no se reconstruye nada");
        comprobar(resultado.meta == referee_scheduler::algorithms::kSinNodo,
                  "no hay nodo meta");
    }

    // ---------------------------------------------------------------------
    // Presupuesto de nodos
    // ---------------------------------------------------------------------
    void probar_presupuesto()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 10, 0),
                    instante(2026, 11, 13, 11, 0), 10));
        instancia.partidos.push_back(
            partido(1, instante(2026, 11, 13, 12, 0),
                    instante(2026, 11, 13, 13, 0), 11));
        instancia.plazas.push_back(plaza(800, 0, Rol::Principal, 1, 20.0));
        instancia.plazas.push_back(plaza(801, 1, Rol::Principal, 1, 25.0));
        instancia.arbitros.push_back(arbitro(
            7, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(11), sede(20)};

        ProveedorConstante proveedor(5);
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        // Con presupuesto 1 no se llega a expandir la raíz.
        {
            Arena arena(dia.n_arbitros());
            OpcionesBusqueda opciones;
            opciones.max_nodos = 1;
            const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia, {}, opciones);

            comprobar(resultado.causa == CausaParada::PresupuestoAgotado,
                      "el presupuesto agotado debe reflejarse en la causa");
            comprobar(!resultado.hay_solucion, "con un solo nodo no hay solucion");
            comprobar(resultado.nodos_creados == 1, "solo se creo la raiz");
            comprobar(resultado.nodos_expandidos == 0, "no se expandio nada");
        }

        // Con presupuesto de sobra, el mismo día se resuelve.
        {
            Arena arena(dia.n_arbitros());
            OpcionesBusqueda opciones;
            opciones.max_nodos = 1000;
            const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia, {}, opciones);

            comprobar(resultado.causa == CausaParada::Optima,
                      "con presupuesto suficiente el dia se resuelve");
            comprobar(resultado.hay_solucion, "debe encontrarse la solucion");
        }
    }

    // ---------------------------------------------------------------------
    // Día sin plazas: la raíz ya es meta
    // ---------------------------------------------------------------------
    void probar_dia_sin_plazas()
    {
        Instancia instancia;
        // Un partido del día sin ninguna plaza: el día no tiene nada que decidir.
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(20)};

        ProveedorConstante proveedor(5);
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_plazas() == 0, "el dia no tiene plazas");
        comprobar(dia.n_arbitros() == 1, "el dia tiene un arbitro");

        Arena arena(dia.n_arbitros());
        const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia);

        comprobar(resultado.causa == CausaParada::Optima, "un dia vacio tiene solucion");
        comprobar(resultado.hay_solucion, "la solucion trivial existe");
        comprobar(resultado.coste == 0, "la solucion trivial cuesta cero");
        comprobar(resultado.nodos_creados == 1, "solo se creo la raiz");
        if (resultado.solucion)
        {
            comprobar(resultado.solucion->decisiones.empty(),
                      "no hay decisiones que reconstruir");
        }
        else
        {
            comprobar(false, "la solucion trivial debe reconstruirse");
        }
    }

    // ---------------------------------------------------------------------
    // Duplicados: dos partidos con principal y asistente, tres árbitros
    // ---------------------------------------------------------------------
    InstanciaDia construir_dia_duplicados(ProveedorDistancias &proveedor)
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 10, 0),
                    instante(2026, 11, 13, 11, 0), 10));
        instancia.partidos.push_back(
            partido(1, instante(2026, 11, 13, 12, 0),
                    instante(2026, 11, 13, 13, 0), 10));

        // Cuatro plazas con la misma sede y la misma hora dos a dos: asignar al
        // árbitro el principal o el asistente del mismo partido deja el mismo
        // estado físico y el mismo coste.
        instancia.plazas.push_back(plaza(900, 0, Rol::Principal, 1, 20.0));
        instancia.plazas.push_back(plaza(901, 0, Rol::Asistente, 1, 12.0));
        instancia.plazas.push_back(plaza(902, 1, Rol::Principal, 1, 20.0));
        instancia.plazas.push_back(plaza(903, 1, Rol::Asistente, 1, 12.0));

        // Tres árbitros con domicilios distintos y la misma distancia a la sede:
        // los empates de coste son lo que hace aparecer los duplicados.
        for (int id = 1; id <= 3; ++id)
        {
            instancia.arbitros.push_back(arbitro(
                id, 1, 20 + id, instante(2026, 11, 13, 9, 0),
                instante(2026, 11, 13, 23, 0)));
        }

        // Sede 10 y domicilios 21, 22 y 23.
        instancia.sedes = {sede(10), sede(21), sede(22), sede(23)};
        return construir_dia(instancia, proveedor);
    }

    void probar_duplicados()
    {
        ProveedorConstante proveedor(5);
        InstanciaDia dia = construir_dia_duplicados(proveedor);

        comprobar(dia.n_plazas() == 4, "el dia de duplicados tiene cuatro plazas");
        comprobar(dia.n_arbitros() == 3, "el dia de duplicados tiene tres arbitros");

        // Oráculo: todos los caminos, sin tabla de estados.
        Arena arena_exhaustiva(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena_exhaustiva, dia);
        const std::int64_t optimo = coste_minimo_exhaustivo(arena_exhaustiva, dia, raiz);

        // Búsqueda real, con tabla y frontera.
        Arena arena(dia.n_arbitros());
        const ResultadoBusqueda resultado = buscar_dijkstra(arena, dia);

        comprobar(resultado.causa == CausaParada::Optima, "el dia tiene solucion");
        comprobar(resultado.hay_solucion, "debe haber solucion");
        comprobar(resultado.coste == optimo,
                  "Dijkstra debe dar el mismo optimo que el recorrido exhaustivo");

        // La tabla tiene que haber colapsado duplicados: si no, no está haciendo
        // su trabajo (cada partido con dos roles genera el mismo estado).
        comprobar(resultado.estados_distintos < resultado.nodos_creados,
                  "la tabla debe colapsar estados duplicados");
        comprobar(resultado.nodos_expandidos <= resultado.estados_distintos,
                  "con cubos exactos no hay reaperturas");

        // Los cubos agrupados no pueden cambiar el óptimo: solo exploran más.
        {
            Arena arena_ancha(dia.n_arbitros());
            OpcionesBusqueda opciones;
            opciones.anchura_cubo = 100; // cubos de 1 euro
            const ResultadoBusqueda ancha =
                buscar_dijkstra(arena_ancha, dia, {}, opciones);
            comprobar(ancha.causa == CausaParada::Optima,
                      "con cubos anchos la busqueda tambien termina");
            comprobar(ancha.coste == optimo,
                      "con cubos anchos el optimo no cambia");
        }
    }

} // namespace

int main()
{
    probar_dos_partidos();
    probar_secundaria_vacia();
    probar_sin_solucion();
    probar_presupuesto();
    probar_dia_sin_plazas();
    probar_duplicados();

    if (fallos == 0)
    {
        std::cout << "test_busqueda: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
