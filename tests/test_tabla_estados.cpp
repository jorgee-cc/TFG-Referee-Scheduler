// Pruebas de `algorithms::TablaEstados` (la lista cerrada de la búsqueda):
// alta de estados nuevos, comparación exacta por nivel + estado, reemplazo solo
// con coste estrictamente menor, marcado del nodo superado, crecimiento de la
// tabla y contadores de memoria.
// Es el ejecutable `referee_scheduler_tabla_tests` de CTest.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "referee_scheduler/algorithms/arena.hpp"
#include "referee_scheduler/algorithms/estado.hpp"
#include "referee_scheduler/algorithms/tabla_estados.hpp"

namespace
{

    using referee_scheduler::algorithms::Arena;
    using referee_scheduler::algorithms::CabeceraNodo;
    using referee_scheduler::algorithms::EntradaTabla;
    using referee_scheduler::algorithms::Estado;
    using referee_scheduler::algorithms::EstadoArbitro;
    using referee_scheduler::algorithms::TablaEstados;
    using referee_scheduler::algorithms::huella_estado;
    using referee_scheduler::algorithms::kFlagSuperado;
    using referee_scheduler::algorithms::kSinNodo;

    /// Árbitros de los estados de prueba: tres bastan para distinguir estados.
    constexpr std::size_t kArbitrosPrueba = 3;

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

    /// Registro de un árbitro del estado de prueba.
    EstadoArbitro registro(std::uint32_t sede, std::uint32_t minuto,
                           std::uint32_t reservado = 0)
    {
        EstadoArbitro estado{};
        estado.sede_id = sede;
        estado.minuto_liberacion = minuto;
        estado.reservado = reservado;
        return estado;
    }

    /// Crea en la arena un nodo con el estado de tres árbitros indicado.
    std::uint32_t crear_nodo(Arena &arena, std::size_t nivel, std::int64_t coste,
                             EstadoArbitro primero, EstadoArbitro segundo,
                             EstadoArbitro tercero)
    {
        Estado estado{}; // el resto de ranuras (capacidad máxima) queda a cero
        estado.arbitros[0] = primero;
        estado.arbitros[1] = segundo;
        estado.arbitros[2] = tercero;

        CabeceraNodo cabecera;
        cabecera.coste_g = coste;
        cabecera.nivel = static_cast<std::uint32_t>(nivel);
        return arena.crear(estado, cabecera);
    }

    // ---------------------------------------------------------------------
    // Estado nuevo, estado repetido y niveles distintos
    // ---------------------------------------------------------------------
    void probar_alta_y_repeticion()
    {
        Arena arena(kArbitrosPrueba);
        TablaEstados tabla;

        const EstadoArbitro r0 = registro(1, 600);
        const EstadoArbitro r1 = registro(2, 610);
        const EstadoArbitro r2 = registro(3, 620);

        comprobar(tabla.size() == 0, "la tabla empieza vacia");
        comprobar((tabla.capacidad() & (tabla.capacidad() - 1)) == 0,
                  "la capacidad es una potencia de dos");

        const std::uint32_t nodo = crear_nodo(arena, 1, 500, r0, r1, r2);

        comprobar(tabla.nodo(arena, 1, arena.estado(nodo)) == kSinNodo,
                  "un estado no insertado no se encuentra");

        comprobar(tabla.mejorar(arena, nodo), "un estado nuevo entra en la tabla");
        comprobar(tabla.size() == 1, "la tabla tiene un estado");
        comprobar(tabla.nodo(arena, 1, arena.estado(nodo)) == nodo,
                  "el estado insertado se recupera");
        comprobar(tabla.nodo(arena, nodo) == nodo,
                  "el acceso por nodo coincide con el acceso por estado");

        // El mismo nodo (o uno idéntico) no duplica la entrada.
        comprobar(!tabla.mejorar(arena, nodo), "reinsertar el mismo nodo no mejora nada");
        comprobar(tabla.size() == 1, "la tabla no crece al repetir el estado");

        // El mismo estado físico en otro nivel es un nodo distinto: lo que queda
        // por decidir no es lo mismo, así que no se puede podar.
        const std::uint32_t otro_nivel = crear_nodo(arena, 2, 500, r0, r1, r2);
        comprobar(tabla.mejorar(arena, otro_nivel),
                  "el mismo estado en otro nivel se registra aparte");
        comprobar(tabla.size() == 2, "hay dos estados distintos");
        comprobar(tabla.nodo(arena, 1, arena.estado(nodo)) == nodo,
                  "el nivel 1 conserva su nodo");
        comprobar(tabla.nodo(arena, 2, arena.estado(otro_nivel)) == otro_nivel,
                  "el nivel 2 tiene su propio nodo");

        // Un cambio en cualquier bit significativo cambia el estado. Los bits
        // `reservado` cuentan: si algún día se usan, dos estados no se confunden.
        const std::uint32_t otra_sede = crear_nodo(arena, 1, 500, registro(4, 600), r1, r2);
        comprobar(tabla.mejorar(arena, otra_sede),
                  "un arbitro en otra sede es un estado distinto");
        const std::uint32_t reservado = crear_nodo(arena, 1, 500, registro(1, 600, 7), r1, r2);
        comprobar(tabla.mejorar(arena, reservado),
                  "un bit reservado distinto es un estado distinto");
        comprobar(tabla.size() == 4, "la tabla tiene ya cuatro estados");
    }

    // ---------------------------------------------------------------------
    // Reemplazo solo con coste estrictamente menor
    // ---------------------------------------------------------------------
    void probar_reemplazo_y_superado()
    {
        Arena arena(kArbitrosPrueba);
        TablaEstados tabla;

        const EstadoArbitro r0 = registro(1, 600);
        const EstadoArbitro r1 = registro(2, 610);
        const EstadoArbitro r2 = registro(3, 620);

        const std::uint32_t primero = crear_nodo(arena, 1, 500, r0, r1, r2);
        comprobar(tabla.mejorar(arena, primero), "el primer camino entra");

        // Peor coste: no se admite y el representante no cambia.
        const std::uint32_t peor = crear_nodo(arena, 1, 900, r0, r1, r2);
        comprobar(!tabla.mejorar(arena, peor),
                  "un camino mas caro al mismo estado se descarta");
        comprobar(tabla.nodo(arena, 1, arena.estado(primero)) == primero,
                  "el representante sigue siendo el barato");
        comprobar((arena.cabecera(peor).flags & kFlagSuperado) == 0,
                  "el nodo descartado no necesita marca: nunca entra en la frontera");

        // Mismo coste: tampoco se admita (el primero llegado gana el empate, lo
        // que hace que la búsqueda sea reproducible).
        const std::uint32_t empate = crear_nodo(arena, 1, 500, r0, r1, r2);
        comprobar(!tabla.mejorar(arena, empate), "un empate de coste se descarta");
        comprobar(tabla.size() == 1, "ni el peor ni el empate crean entradas");

        // Mejor coste: se sustituye el representante y el viejo queda marcado.
        const std::uint32_t mejor = crear_nodo(arena, 1, 400, r0, r1, r2);
        comprobar(tabla.mejorar(arena, mejor), "un camino mas barato si entra");
        comprobar(tabla.size() == 1, "el estado sigue siendo uno solo");
        comprobar(tabla.nodo(arena, 1, arena.estado(primero)) == mejor,
                  "el representante pasa a ser el nodo barato");
        comprobar((arena.cabecera(primero).flags & kFlagSuperado) != 0,
                  "el nodo sustituido queda marcado como superado");
        comprobar((arena.cabecera(mejor).flags & kFlagSuperado) == 0,
                  "el nodo que sustituye no queda marcado");

        // Y el nodo superado ya no puede volver: su coste es mayor.
        comprobar(!tabla.mejorar(arena, primero),
                  "un nodo superado no recupera la entrada");
        comprobar(tabla.nodo(arena, 1, arena.estado(mejor)) == mejor,
                  "el representante no cambia al reintentar el nodo viejo");
    }

    // ---------------------------------------------------------------------
    // Crecimiento, búsqueda entre colisiones y reserva
    // ---------------------------------------------------------------------
    void probar_crecimiento_y_colisiones()
    {
        Arena arena(kArbitrosPrueba);
        TablaEstados tabla(8); // capacidad minima: fuerza colisiones y rehash

        constexpr std::size_t kEstados = 200;
        std::vector<std::uint32_t> nodos;
        nodos.reserve(kEstados);

        // 200 estados distintos que solo cambian en el minuto de liberación del
        // primer árbitro: con capacidad 8 hay colisiones de sobra.
        for (std::size_t i = 0; i < kEstados; ++i)
        {
            const auto minuto = static_cast<std::uint32_t>(i % 1440);
            const std::uint32_t nodo = crear_nodo(
                arena, i % 7, static_cast<std::int64_t>(i),
                registro(1, minuto), registro(2, 610), registro(3, 620));
            comprobar(tabla.mejorar(arena, nodo), "cada estado nuevo entra en la tabla");
            nodos.push_back(nodo);
        }

        comprobar(tabla.size() == kEstados, "deben registrarse todos los estados");
        // Ninguna colisión puede perder entradas: todas se recuperan exactas.
        for (std::size_t i = 0; i < kEstados; ++i)
        {
            comprobar(tabla.nodo(arena, nodos[i]) == nodos[i],
                      "cada estado debe recuperarse aunque haya colisiones");
        }

        // Factor de carga 0,5: siempre queda hueco libre y la capacidad es
        // potencia de dos.
        comprobar(tabla.capacidad() >= 2 * tabla.size(),
                  "la tabla crece antes de llenarse");
        comprobar((tabla.capacidad() & (tabla.capacidad() - 1)) == 0,
                  "la capacidad sigue siendo potencia de dos");
        comprobar(tabla.memoria_reservada() ==
                      tabla.capacidad() * sizeof(EntradaTabla),
                  "la memoria reservada es capacidad por entrada");

        // Reservar por adelantado no pierde nada y crece a la potencia de dos
        // necesaria para mantener el factor de carga.
        tabla.reservar(1000);
        comprobar(tabla.capacidad() >= 2000, "reservar deja huecos para el doble");
        comprobar(tabla.size() == kEstados, "reservar conserva las entradas");
        for (std::size_t i = 0; i < kEstados; ++i)
        {
            comprobar(tabla.nodo(arena, nodos[i]) == nodos[i],
                      "reservar conserva la busqueda de cada estado");
        }
    }

    // ---------------------------------------------------------------------
    // Huella: el nivel forma parte de la clave
    // ---------------------------------------------------------------------
    void probar_huella()
    {
        Arena arena(kArbitrosPrueba);
        const std::uint32_t nodo = crear_nodo(arena, 0, 0, registro(1, 600),
                                              registro(2, 610), registro(3, 620));
        const std::span<const EstadoArbitro> estado = arena.estado(nodo);

        // La huella es determinista y depende del nivel.
        comprobar(huella_estado(0, estado) == huella_estado(0, estado),
                  "la huella es determinista");
        comprobar(huella_estado(0, estado) != huella_estado(1, estado),
                  "el nivel cambia la huella");

        // Y de los bytes del estado.
        const std::uint32_t otro = crear_nodo(arena, 0, 0, registro(9, 600),
                                              registro(2, 610), registro(3, 620));
        comprobar(huella_estado(0, estado) != huella_estado(0, arena.estado(otro)),
                  "un estado distinto da una huella distinta");
    }

} // namespace

int main()
{
    probar_alta_y_repeticion();
    probar_reemplazo_y_superado();
    probar_crecimiento_y_colisiones();
    probar_huella();

    if (fallos == 0)
    {
        std::cout << "test_tabla_estados: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
