// Pruebas del paso 3 de la búsqueda: estado inicial, nodo raíz, generación de
// hijos (asignación, plaza vacía, llegada tarde y centinelas) y reconstrucción
// de la solución a partir de `CabeceraNodo::decision`.
//
// Se usan instancias pequeñas construidas con proveedores de distancia fijos,
// igual que en `test_instancia.cpp`, para que los costes esperados sean
// exactos y no dependan de la red.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <span>
#include <utility>
#include <vector>

#include "referee_scheduler/algorithms/construir_instancia.hpp"
#include "referee_scheduler/algorithms/transiciones.hpp"
#include "referee_scheduler/domain/instancia.hpp"

namespace
{

    using referee_scheduler::algorithms::Arena;
    using referee_scheduler::algorithms::CabeceraNodo;
    using referee_scheduler::algorithms::DecisionDia;
    using referee_scheduler::algorithms::Estado;
    using referee_scheduler::algorithms::EstadoArbitro;
    using referee_scheduler::algorithms::GrupoDia;
    using referee_scheduler::algorithms::InstanciaDia;
    using referee_scheduler::algorithms::Minutos;
    using referee_scheduler::algorithms::ParametrosObjetivo;
    using referee_scheduler::algorithms::ProveedorDistancias;
    using referee_scheduler::algorithms::agrupar_por_dia;
    using referee_scheduler::algorithms::construir_instancia_dia;
    using referee_scheduler::algorithms::crear_raiz;
    using referee_scheduler::algorithms::es_meta;
    using referee_scheduler::algorithms::estado_inicial;
    using referee_scheduler::algorithms::generar_hijos;
    using referee_scheduler::algorithms::kDecisionVacia;
    using referee_scheduler::algorithms::kMaxArbitros;
    using referee_scheduler::algorithms::kMinutosInalcanzable;
    using referee_scheduler::algorithms::kSinDecision;
    using referee_scheduler::algorithms::kSinNodo;
    using referee_scheduler::algorithms::reconstruir_solucion;
    using referee_scheduler::algorithms::tiene_solucion_estatica;
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
        assert(!grupos.empty() && "la instancia de prueba debe tener un dia");
        return construir_instancia_dia(instancia, grupos.front(), proveedor);
    }

    // ---------------------------------------------------------------------
    // Estado inicial y raíz
    // ---------------------------------------------------------------------
    void probar_estado_inicial_y_raiz()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.plazas.push_back(plaza(100, 0, Rol::Asistente, 1, 12.0));

        // Se meten desordenados: el id compacto debe salir por id global.
        instancia.arbitros.push_back(arbitro(
            5, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.arbitros.push_back(arbitro(
            3, 1, 21, instante(2026, 11, 13, 10, 0),
            instante(2026, 11, 13, 22, 0)));

        instancia.sedes = {sede(10), sede(20), sede(21)};

        ProveedorConstante proveedor(5);
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_arbitros() == 2, "deben compactarse dos arbitros");
        comprobar(dia.n_sedes() == 3, "deben compactarse tres ubicaciones");
        if (dia.n_arbitros() != 2 || dia.n_sedes() != 3)
        {
            return;
        }

        // Orden global: 3 -> compacto 0 (domicilio 21 -> índice 2),
        // 5 -> compacto 1 (domicilio 20 -> índice 1).
        comprobar(dia.arbitros[0].id_global == 3, "el primer compacto es el 3");
        comprobar(dia.arbitros[1].id_global == 5, "el segundo compacto es el 5");

        const Estado estado = estado_inicial(dia);

        comprobar(estado.arbitros[0].sede_id == 2,
                  "el arbitro 3 empieza en su domicilio compacto");
        comprobar(estado.arbitros[0].minuto_liberacion == 10 * 60,
                  "el arbitro 3 empieza a las 10:00");
        comprobar(estado.arbitros[1].sede_id == 1,
                  "el arbitro 5 empieza en su domicilio compacto");
        comprobar(estado.arbitros[1].minuto_liberacion == 9 * 60,
                  "el arbitro 5 empieza a las 9:00");

        for (std::size_t i = 0; i < dia.n_arbitros(); ++i)
        {
            comprobar(estado.arbitros[i].reservado == 0,
                      "los bits reservados empiezan a cero");
        }

        // La cola de capacidad no se usa, pero debe quedar limpia.
        for (std::size_t i = dia.n_arbitros(); i < kMaxArbitros; ++i)
        {
            comprobar(estado.arbitros[i].sede_id == 0 &&
                          estado.arbitros[i].minuto_liberacion == 0 &&
                          estado.arbitros[i].reservado == 0,
                      "las ranuras sobrantes deben quedar a cero");
        }

        Arena arena(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena, dia);

        comprobar(arena.nodos_creados() == 1, "la raiz es el primer nodo");
        comprobar(arena.cabecera(raiz).coste_g == 0, "la raiz cuesta cero");
        comprobar(arena.cabecera(raiz).padre == kSinNodo,
                  "la raiz no tiene padre");
        comprobar(arena.cabecera(raiz).nivel == 0, "la raiz esta en el nivel 0");
        comprobar(arena.cabecera(raiz).decision == kSinDecision,
                  "la raiz no tiene decision");
        comprobar(!es_meta(arena, dia, raiz),
                  "con una plaza pendiente la raiz no es meta");

        const std::span<const EstadoArbitro> vista = arena.estado(raiz);
        comprobar(vista[0].sede_id == estado.arbitros[0].sede_id &&
                      vista[0].minuto_liberacion ==
                          estado.arbitros[0].minuto_liberacion,
                  "la arena copia el estado inicial");
    }

    // ---------------------------------------------------------------------
    // Asignación y plaza vacía
    // ---------------------------------------------------------------------
    void probar_hijos_asignacion_y_vacia()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.plazas.push_back(plaza(200, 0, Rol::Asistente, 1, 12.0));
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(20)};

        // Orden compacto: sede 10 -> 0, domicilio 20 -> 1. Viaje 5 en ambos
        // sentidos.
        ProveedorFijo proveedor({0, 5, 5, 0});
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_arbitros() == 1 && dia.n_plazas() == 1,
                  "la instancia de prueba tiene un arbitro y una plaza");
        if (dia.n_arbitros() != 1 || dia.n_plazas() != 1)
        {
            return;
        }

        comprobar(dia.n_candidatos(0) == 1,
                  "el arbitro es candidato de la plaza");

        Arena arena(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena, dia);

        std::vector<std::uint32_t> hijos;
        generar_hijos(arena, dia, raiz, ParametrosObjetivo{}, hijos);

        comprobar(hijos.size() == 2,
                  "debe haber un hijo de asignacion y otro de plaza vacia");
        if (hijos.size() != 2)
        {
            return;
        }

        // Hijo 0: asignar. Primer viaje desde casa:
        // 1 + 5*14 (ida) + 5*14 (retorno previsto) = 141 céntimos.
        const std::uint32_t asignado = hijos[0];
        const CabeceraNodo &cab_asignado = arena.cabecera(asignado);
        comprobar(cab_asignado.coste_g == 141,
                  "el primer viaje desde casa cuesta 141 centimos");
        comprobar(cab_asignado.padre == raiz, "el hijo apunta a la raiz");
        comprobar(cab_asignado.nivel == 1, "el hijo sube al nivel 1");
        comprobar(cab_asignado.decision == 0,
                  "la decision guarda el id compacto del arbitro");

        const std::span<const EstadoArbitro> estado_asignado =
            arena.estado(asignado);
        comprobar(estado_asignado[0].sede_id == 0,
                  "el arbitro queda en la sede del partido");
        comprobar(estado_asignado[0].minuto_liberacion == 19 * 60,
                  "el arbitro queda libre al acabar el partido");
        comprobar(estado_asignado[0].reservado == 0,
                  "la asignacion no toca los bits reservados");

        // Hijo 1: dejar vacía una plaza secundaria. Cuesta su retribución y no
        // cambia el estado.
        const std::uint32_t vacia = hijos[1];
        const CabeceraNodo &cab_vacia = arena.cabecera(vacia);
        comprobar(cab_vacia.coste_g == 1200,
                  "dejar vacia la plaza cuesta su retribucion");
        comprobar(cab_vacia.decision == kDecisionVacia,
                  "la decision marca la plaza como vacia");

        const std::span<const EstadoArbitro> estado_vacio = arena.estado(vacia);
        comprobar(estado_vacio[0].sede_id == 1 &&
                      estado_vacio[0].minuto_liberacion == 9 * 60,
                  "dejar vacia la plaza no cambia el estado");

        comprobar(es_meta(arena, dia, asignado) &&
                      es_meta(arena, dia, vacia),
                  "los dos hijos completan la unica plaza");

        // El estado del padre no se modifica al crear los hijos.
        const std::span<const EstadoArbitro> estado_raiz = arena.estado(raiz);
        comprobar(estado_raiz[0].sede_id == 1 &&
                      estado_raiz[0].minuto_liberacion == 9 * 60,
                  "generar hijos no modifica al padre");

        // La variante con callback debe producir exactamente el mismo orden.
        Arena arena_callback(dia.n_arbitros());
        const std::uint32_t raiz_callback = crear_raiz(arena_callback, dia);
        std::vector<std::uint32_t> hijos_callback;
        generar_hijos(arena_callback, dia, raiz_callback, ParametrosObjetivo{},
                      [&hijos_callback](std::uint32_t hijo)
                      {
                          hijos_callback.push_back(hijo);
                      });

        comprobar(hijos_callback == hijos,
                  "el callback y el vector generan los mismos nodos en el mismo orden");
    }

    // ---------------------------------------------------------------------
    // Plaza principal sin candidatos y llegada tarde
    // ---------------------------------------------------------------------
    void probar_principal_sin_candidatos()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.plazas.push_back(plaza(300, 0, Rol::Principal, 2, 30.0));
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(20)};

        ProveedorFijo proveedor({0, 5, 5, 0});
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_candidatos(0) == 0,
                  "el nivel 1 no llega al nivel 2 exigido");
        comprobar(!tiene_solucion_estatica(dia),
                  "una principal sin candidatos hace el dia infeasible");

        Arena arena(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena, dia);
        std::vector<std::uint32_t> hijos;
        generar_hijos(arena, dia, raiz, ParametrosObjetivo{}, hijos);

        comprobar(hijos.empty(),
                  "una principal sin candidatos no genera hijos");
    }

    void probar_llegada_tarde()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.partidos.push_back(
            partido(1, instante(2026, 11, 13, 19, 5),
                    instante(2026, 11, 13, 20, 0), 11));
        instancia.plazas.push_back(plaza(400, 0, Rol::Principal, 1, 20.0));
        instancia.plazas.push_back(plaza(401, 1, Rol::Asistente, 1, 10.0));
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(11), sede(20)};

        // Orden compacto: 10 -> 0, 11 -> 1, domicilio 20 -> 2.
        // El primer partido acaba a las 19:00 y el segundo empieza a las 19:05,
        // pero el viaje 0->1 cuesta 10 minutos: se llega a las 19:10.
        ProveedorFijo proveedor({
            0, 10, 5,   // 10
            10, 0, 5,   // 11
            5, 5, 0,    // 20
        });
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_plazas() == 2, "deben existir dos plazas");
        comprobar(dia.n_candidatos(0) == 1 && dia.n_candidatos(1) == 1,
                  "los dos partidos tienen candidato estatico");

        Arena arena(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena, dia);
        std::vector<std::uint32_t> primeros;
        generar_hijos(arena, dia, raiz, ParametrosObjetivo{}, primeros);

        comprobar(primeros.size() == 1,
                  "la principal del primer partido solo genera la asignacion");
        if (primeros.empty())
        {
            return;
        }

        const std::uint32_t tras_primero = primeros.front();
        comprobar(arena.cabecera(tras_primero).nivel == 1,
                  "tras el primer partido se esta en el nivel 1");

        std::vector<std::uint32_t> segundos;
        generar_hijos(arena, dia, tras_primero, ParametrosObjetivo{}, segundos);

        comprobar(segundos.size() == 1,
                  "no se puede asignar el segundo: solo queda la rama vacia");
        if (segundos.size() == 1)
        {
            comprobar(arena.cabecera(segundos[0]).decision == kDecisionVacia,
                      "la unica rama posible es dejar vacia la secundaria");
        }

        // Si el segundo partido fuera principal, el nodo moriría sin hijos.
        Instancia principal = instancia;
        principal.plazas[1].rol = Rol::Principal;
        ProveedorFijo proveedor_principal({
            0, 10, 5,
            10, 0, 5,
            5, 5, 0,
        });
        const InstanciaDia dia_principal =
            construir_dia(principal, proveedor_principal);

        Arena arena_principal(dia_principal.n_arbitros());
        const std::uint32_t raiz_principal =
            crear_raiz(arena_principal, dia_principal);
        std::vector<std::uint32_t> primeros_principal;
        generar_hijos(arena_principal, dia_principal, raiz_principal,
                      ParametrosObjetivo{}, primeros_principal);

        if (!primeros_principal.empty())
        {
            std::vector<std::uint32_t> segundos_principal;
            generar_hijos(arena_principal, dia_principal,
                          primeros_principal.front(), ParametrosObjetivo{},
                          segundos_principal);
            comprobar(segundos_principal.empty(),
                      "una principal a la que no se llega no genera hijos");
        }
    }

    // ---------------------------------------------------------------------
    // Reconstrucción de una solución completa
    // ---------------------------------------------------------------------
    void probar_reconstruccion()
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

        // Orden compacto: 10 -> 0, 11 -> 1, domicilio 20 -> 2. Todos los viajes
        // cuestan 5 minutos.
        ProveedorFijo proveedor({
            0, 5, 5,
            5, 0, 5,
            5, 5, 0,
        });
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_plazas() == 2, "deben existir dos plazas");
        comprobar(dia.n_candidatos(0) == 1 && dia.n_candidatos(1) == 1,
                  "el arbitro es candidato de las dos plazas");

        Arena arena(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena, dia);

        std::vector<std::uint32_t> primeros;
        generar_hijos(arena, dia, raiz, ParametrosObjetivo{}, primeros);
        comprobar(primeros.size() == 1,
                  "la primera principal solo puede asignarse");
        if (primeros.empty())
        {
            return;
        }

        const std::uint32_t tras_primero = primeros.front();
        std::vector<std::uint32_t> segundos;
        generar_hijos(arena, dia, tras_primero, ParametrosObjetivo{}, segundos);
        comprobar(segundos.size() == 1,
                  "la segunda principal solo puede asignarse");
        if (segundos.empty())
        {
            return;
        }

        const std::uint32_t meta = segundos.front();
        comprobar(es_meta(arena, dia, meta), "la cadena termina en una meta");

        // Primer partido: 1 + 5*14 + 5*14 = 141.
        // Segundo: 1 + (5 de viaje + 55 de espera)*14 = 841. Total 982.
        const auto solucion = reconstruir_solucion(arena, dia, meta);
        comprobar(solucion.coste == 982,
                  "el coste acumulado de la solucion es 982 centimos");
        comprobar(solucion.decisiones.size() == 2,
                  "debe haber una decision por plaza");

        if (solucion.decisiones.size() == 2)
        {
            const DecisionDia &primera = solucion.decisiones[0];
            const DecisionDia &segunda = solucion.decisiones[1];

            comprobar(primera.plaza_id == 500 && primera.partido_id == 0,
                      "la primera decision es la plaza 500 del partido 0");
            comprobar(segunda.plaza_id == 501 && segunda.partido_id == 1,
                      "la segunda decision es la plaza 501 del partido 1");
            comprobar(primera.arbitro_global == 7 &&
                          segunda.arbitro_global == 7,
                      "las dos plazas se asignan al arbitro global 7");
        }

        // El estado final deja al árbitro en la última sede y libre a las 13:00.
        const std::span<const EstadoArbitro> estado_meta = arena.estado(meta);
        comprobar(estado_meta[0].sede_id == 1,
                  "el arbitro termina en la sede del segundo partido");
        comprobar(estado_meta[0].minuto_liberacion == 13 * 60,
                  "el arbitro termina libre a las 13:00");
    }

    // ---------------------------------------------------------------------
    // Factibilidad estática y casos triviales
    // ---------------------------------------------------------------------
    void probar_tiene_solucion_estatica()
    {
        // Un día sin árbitros y con una plaza secundaria es resoluble dejándola
        // vacía; con una principal, no.
        {
            Instancia instancia;
            instancia.partidos.push_back(
                partido(0, instante(2026, 11, 13, 18, 0),
                        instante(2026, 11, 13, 19, 0), 10));
            instancia.plazas.push_back(plaza(600, 0, Rol::Asistente, 1, 10.0));
            instancia.sedes = {sede(10)};

            ProveedorConstante proveedor(5);
            const InstanciaDia dia = construir_dia(instancia, proveedor);
            comprobar(dia.n_arbitros() == 0 && dia.n_plazas() == 1,
                      "el dia de prueba no tiene arbitros");
            comprobar(tiene_solucion_estatica(dia),
                      "una secundaria sin arbitros se puede dejar vacia");
        }

        {
            Instancia instancia;
            instancia.partidos.push_back(
                partido(0, instante(2026, 11, 13, 18, 0),
                        instante(2026, 11, 13, 19, 0), 10));
            instancia.plazas.push_back(plaza(601, 0, Rol::Principal, 1, 10.0));
            instancia.sedes = {sede(10)};

            ProveedorConstante proveedor(5);
            const InstanciaDia dia = construir_dia(instancia, proveedor);
            comprobar(!tiene_solucion_estatica(dia),
                      "una principal sin arbitros es infeasible");
        }

        // Un día con árbitros pero sin plazas: la raíz ya es meta.
        {
            Instancia instancia;
            instancia.partidos.push_back(
                partido(0, instante(2026, 11, 13, 18, 0),
                        instante(2026, 11, 13, 19, 0), 10));
            instancia.arbitros.push_back(arbitro(
                1, 1, 20, instante(2026, 11, 13, 9, 0),
                instante(2026, 11, 13, 23, 0)));
            instancia.sedes = {sede(10), sede(20)};

            ProveedorFijo proveedor({0, 5, 5, 0});
            const InstanciaDia dia = construir_dia(instancia, proveedor);
            comprobar(dia.n_plazas() == 0, "el partido no tiene plazas");
            comprobar(tiene_solucion_estatica(dia),
                      "un dia sin plazas no tiene principales pendientes");

            Arena arena(dia.n_arbitros());
            const std::uint32_t raiz = crear_raiz(arena, dia);
            comprobar(es_meta(arena, dia, raiz),
                      "sin plazas la raiz ya es una solucion");
        }
    }

    // ---------------------------------------------------------------------
    // Centinela de vuelta
    // ---------------------------------------------------------------------
    void probar_centinela_de_vuelta()
    {
        Instancia instancia;
        instancia.partidos.push_back(
            partido(0, instante(2026, 11, 13, 18, 0),
                    instante(2026, 11, 13, 19, 0), 10));
        instancia.plazas.push_back(plaza(700, 0, Rol::Asistente, 1, 10.0));
        instancia.arbitros.push_back(arbitro(
            1, 1, 20, instante(2026, 11, 13, 9, 0),
            instante(2026, 11, 13, 23, 0)));
        instancia.sedes = {sede(10), sede(20)};

        // Orden compacto: sede 10 -> 0, domicilio 20 -> 1.
        // Hay ida (fila 1, columna 0 = 5) pero no vuelta (fila 0, columna 1).
        ProveedorFijo proveedor({
            0, kMinutosInalcanzable,
            5, 0,
        });
        const InstanciaDia dia = construir_dia(instancia, proveedor);

        comprobar(dia.n_candidatos(0) == 0,
                  "la fase 5 descarta la plaza sin ruta de vuelta");
        comprobar(tiene_solucion_estatica(dia),
                  "la plaza secundaria se puede dejar vacia aunque no haya vuelta");

        Arena arena(dia.n_arbitros());
        const std::uint32_t raiz = crear_raiz(arena, dia);
        std::vector<std::uint32_t> hijos;
        generar_hijos(arena, dia, raiz, ParametrosObjetivo{}, hijos);

        comprobar(hijos.size() == 1,
                  "sin candidato solo queda la rama de plaza vacia");
        if (hijos.size() == 1)
        {
            comprobar(arena.cabecera(hijos[0]).decision == kDecisionVacia,
                      "la unica rama es dejar la plaza vacia");
        }
    }

} // namespace

int main()
{
    probar_estado_inicial_y_raiz();
    probar_hijos_asignacion_y_vacia();
    probar_principal_sin_candidatos();
    probar_llegada_tarde();
    probar_reconstruccion();
    probar_tiene_solucion_estatica();
    probar_centinela_de_vuelta();

    if (fallos == 0)
    {
        std::cout << "OK: transiciones\n";
        return 0;
    }

    std::cerr << fallos << " fallo(s)\n";
    return 1;
}
