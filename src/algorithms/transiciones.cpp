// Implementación de las transiciones del paso 3: estado inicial, raíz,
// generador de hijos con vector de salida y reconstrucción de la solución.
//
// El generador con callback vive en la cabecera porque es una plantilla. Esta
// unidad compila la variante con `std::vector`, que reutiliza el mismo núcleo
// `detalle::generar_hijos_impl`: así no hay dos copias de la lógica de coste.
//
// Reconstruir la solución no compara estados: cada nodo guarda en su cabecera
// la decisión que lo creó (árbitro compacto, plaza vacía o raíz).
//
// La transición de una plaza es la única escritura de 4 B del estado:
//   EstadoArbitro::sede_id          <- plaza.sede
//   EstadoArbitro::minuto_liberacion <- plaza.minuto_fin
//   EstadoArbitro::reservado        <- 0
// El resto del estado se copia tal cual del padre; por eso el estado es la
// mínima estadística suficiente y no guarda qué partidos hizo cada árbitro.

#include "referee_scheduler/algorithms/transiciones.hpp"

#include <algorithm>
#include <cassert>

namespace referee_scheduler::algorithms
{

    Estado estado_inicial(const InstanciaDia &dia)
    {
        assert(dia.n_arbitros() <= kMaxArbitros &&
               "n_arbitros no cabe en Estado: revisa la capacidad de compilacion");
        // Un día sin sedes solo es válido si tampoco hay árbitros; si hay
        // árbitros, cada uno debe tener un domicilio compacto en la matriz.
        assert((dia.n_arbitros() == 0 || dia.n_sedes() > 0) &&
               "un dia con arbitros debe tener al menos una sede");

        // `Estado{}` deja a cero también las ranuras [N, kMaxArbitros), que la
        // arena no copia pero que conviene mantener limpias.
        Estado estado{};

        for (std::size_t i = 0; i < dia.n_arbitros(); ++i)
        {
            const ArbitroDia &arbitro = dia.arbitros[i];

            assert(arbitro.domicilio < dia.n_sedes());
            assert(arbitro.primer_inicio >= 0 &&
                   arbitro.primer_inicio < 2048 &&
                   "primer_inicio debe caber en los 11 bits del estado");

            // Sede = domicilio es la marca que usa `coste_asignar` para el
            // régimen "todavía no ha salido de casa". Es válida porque las
            // sedes de partido y los domicilios son conjuntos disjuntos.
            estado.arbitros[i].sede_id = arbitro.domicilio;

            // En el estado inicial la liberación no es una hora de salida: es
            // el primer minuto en que puede empezar un partido. Puede salir de
            // casa antes sin que eso se guarde en el estado.
            estado.arbitros[i].minuto_liberacion =
                static_cast<std::uint32_t>(arbitro.primer_inicio);

            // Los 11 bits reservados quedan libres para una restricción futura.
            estado.arbitros[i].reservado = 0;
        }

        return estado;
    }

    std::uint32_t crear_raiz(Arena &arena, const InstanciaDia &dia)
    {
        assert(arena.n_arbitros() == dia.n_arbitros());
        assert(dia.n_arbitros() > 0 &&
               "la arena no puede representar un dia sin arbitros");
        assert(arena.nodos_creados() == 0 &&
               "la raiz debe ser el primer nodo de una arena nueva");

        // El estado inicial se construye en la vista de capacidad máxima y la
        // arena copia solo `[0, n_arbitros)`.
        const Estado inicial = estado_inicial(dia);

        // La raíz no representa ninguna decisión: coste 0, sin padre y en el
        // nivel 0. Si el día no tiene plazas, la raíz ya será meta.
        CabeceraNodo raiz;
        raiz.coste_g = 0;
        raiz.padre = kSinNodo;
        raiz.nivel = 0;
        raiz.flags = 0;
        raiz.decision = kSinDecision;

        return arena.crear(inicial, raiz);
    }

    bool es_meta(const Arena &arena, const InstanciaDia &dia, std::uint32_t nodo)
    {
        assert(nodo < arena.nodos_creados());
        // El nivel es el número de plazas ya decididas; la instancia ordena las
        // plazas cronológicamente, así que nivel == P significa solución.
        return arena.cabecera(nodo).nivel == dia.n_plazas();
    }

    bool tiene_solucion_estatica(const InstanciaDia &dia) noexcept
    {
        // Una plaza principal sin ningún candidato estático no se puede cubrir
        // nunca: el generador no puede inventar árbitros ni dejarla vacía.
        // Las secundarias sí pueden quedarse vacías pagando su retribución.
        // No se comprueba la factibilidad dinámica: un candidato puede existir
        // y aun así no llegar a tiempo desde la sede en la que esté.
        for (std::size_t plaza = 0; plaza < dia.n_plazas(); ++plaza)
        {
            if (dia.plazas[plaza].es_principal() &&
                dia.n_candidatos(plaza) == 0)
            {
                return false;
            }
        }

        return true;
    }

    void generar_hijos(Arena &arena,
                       const InstanciaDia &dia,
                       std::uint32_t nodo,
                       const ParametrosObjetivo &parametros,
                       std::vector<std::uint32_t> &hijos)
    {
        // El contrato de esta sobrecarga es que el vector se limpia; la versión
        // con callback no toca ningún contenedor del llamador.
        hijos.clear();

        // Se delega en la variante con callback para no duplicar la lógica de
        // coste y de escritura del estado. El vector solo acumula los ids; si
        // el llamador lo reutiliza entre expansiones, `push_back` no reserva de
        // nuevo una vez alcanzada la capacidad máxima de ramificación.
        generar_hijos(arena, dia, nodo, parametros,
                      [&hijos](std::uint32_t hijo)
                      {
                          hijos.push_back(hijo);
                      });
    }

    SolucionDia reconstruir_solucion(const Arena &arena,
                                     const InstanciaDia &dia,
                                     std::uint32_t meta)
    {
        assert(es_meta(arena, dia, meta) &&
               "solo se reconstruye desde una solucion completa");

        SolucionDia solucion;
        // El coste ya está completo en la meta: incluye viajes, esperas,
        // retornos telescópicos y retribuciones de plazas secundarias vacías.
        solucion.coste = arena.cabecera(meta).coste_g;
        solucion.decisiones.reserve(dia.n_plazas());

        // Se sube de meta a raíz. Cada nodo con nivel = p + 1 decidió plazas[p]:
        // por eso la decisión se consulta con `nivel - 1`, no con `nivel`.
        for (std::uint32_t nodo = meta; nodo != kSinNodo;)
        {
            const CabeceraNodo &cab = arena.cabecera(nodo);

            // La raíz no tiene decisión: se ha llegado al principio de la
            // cadena y ya están todas las plazas recogidas.
            if (decision_es_raiz(cab.decision))
            {
                assert(cab.padre == kSinNodo);
                break;
            }

            assert(cab.nivel > 0);
            const std::size_t nivel = cab.nivel - 1;
            assert(nivel < dia.n_plazas());

            const PlazaDia &plaza = dia.plazas[nivel];

            DecisionDia decision;
            decision.plaza_id = plaza.plaza_id;
            decision.partido_id = plaza.partido_id;

            if (decision_es_asignacion(cab.decision))
            {
                assert(cab.decision < dia.n_arbitros());
                // La cabecera guarda el id compacto; el resultado usa el global.
                decision.arbitro_global =
                    dia.arbitros[cab.decision].id_global;
            }
            else
            {
                assert(decision_es_vacia(cab.decision));
                decision.arbitro_global = -1;
            }

            solucion.decisiones.push_back(decision);
            nodo = cab.padre;
        }

        // Se subió de meta a raíz, así que el orden natural es el inverso.
        std::reverse(solucion.decisiones.begin(), solucion.decisiones.end());
        assert(solucion.decisiones.size() == dia.n_plazas());

        return solucion;
    }

} // namespace referee_scheduler::algorithms
