// Pruebas de `algorithms::Arena`: tamaño de bloque, copia de solo las
// `n_arbitros` entradas significativas, longitud de los spans, estabilidad de
// las direcciones al crear nodos en muchos bloques y contadores de memoria.
// Se ejecuta para varios tamaños de día (1, 5, 300 y 600 árbitros) y es el
// ejecutable `referee_scheduler_arena_tests` de CTest.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

#include "referee_scheduler/algorithms/arena.hpp"
#include "referee_scheduler/algorithms/estado.hpp"

namespace {

using referee_scheduler::algorithms::Arena;
using referee_scheduler::algorithms::CabeceraNodo;
using referee_scheduler::algorithms::Estado;
using referee_scheduler::algorithms::EstadoArbitro;
using referee_scheduler::algorithms::kMaxArbitros;
using referee_scheduler::algorithms::kSinNodo;

int fallos = 0;

/// Registra un fallo sin abortar: así se ven todos los problemas de una vez.
void comprobar(bool condicion, const char* mensaje) {
    if (!condicion) {
        std::cerr << "FALLO: " << mensaje << '\n';
        ++fallos;
    }
}

/// Rellena `[0, n)` con valores reconocibles y `[n, kMaxArbitros)` con basura.
///
/// La cola hace de centinela: si la arena copiase de más, el nodo tendría
/// valores distintos de los esperados.
void preparar_estado(Estado& estado, std::size_t n) {
    for (std::size_t i = 0; i < kMaxArbitros; ++i) {
        estado.arbitros[i].sede_id = 1023;
        estado.arbitros[i].minuto_liberacion = 2047;
        estado.arbitros[i].reservado = 2047;
    }
    for (std::size_t i = 0; i < n; ++i) {
        estado.arbitros[i].sede_id = static_cast<std::uint32_t>((i * 7) % 1024);
        estado.arbitros[i].minuto_liberacion =
            static_cast<std::uint32_t>(i % 1440);
        estado.arbitros[i].reservado = static_cast<std::uint32_t>(i % 2048);
    }
}

/// Tamaño de un nodo con `n` árbitros y comportamiento con la cola del estado.
void probar_tamano_y_copia(std::size_t n) {
    // 1) El tamaño de bloque es 24 + 4n redondeado a 8.
    const std::size_t tam = Arena::calcular_tam_bloque(n);
    comprobar(tam >= sizeof(CabeceraNodo) + 4 * n,
              "el bloque debe caber cabecera + 4n");
    comprobar(tam - (sizeof(CabeceraNodo) + 4 * n) < 8,
              "el relleno de alineacion debe ser menor que 8 B");
    comprobar(tam % alignof(CabeceraNodo) == 0,
              "el tamano de bloque debe respetar la alineacion de la cabecera");

    // 2) Un nodo creado desde un `Estado` con cola basura copia solo [0, n).
    Estado origen{};
    preparar_estado(origen, n);

    Arena arena(n);
    comprobar(arena.n_arbitros() == n, "n_arbitros debe coincidir con el constructor");
    comprobar(arena.tam_bloque() == tam, "tam_bloque debe coincidir con la formula");
    comprobar(arena.nodos_creados() == 0, "la arena debe nacer vacia");

    CabeceraNodo cabecera;
    cabecera.coste_g = -42;
    cabecera.padre = kSinNodo;
    cabecera.nivel = 3;
    cabecera.flags = 5;
    cabecera.reservado = 0;

    const std::uint32_t nodo = arena.crear(origen, cabecera);

    comprobar(nodo == 0, "el primer nodo debe tener indice 0");
    comprobar(arena.nodos_creados() == 1, "crear debe incrementar el contador");
    comprobar(arena.cabecera(nodo).coste_g == -42, "coste_g debe conservarse");
    comprobar(arena.cabecera(nodo).padre == kSinNodo, "padre debe conservarse");
    comprobar(arena.cabecera(nodo).nivel == 3, "nivel debe conservarse");
    comprobar(arena.cabecera(nodo).flags == 5, "flags debe conservarse");

    const std::span<const EstadoArbitro> vista = arena.estado(nodo);
    comprobar(vista.size() == n, "el span debe medir exactamente n_arbitros");
    for (std::size_t i = 0; i < n; ++i) {
        comprobar(vista[i].sede_id == origen.arbitros[i].sede_id,
                  "sede_id debe copiarse");
        comprobar(vista[i].minuto_liberacion ==
                      origen.arbitros[i].minuto_liberacion,
                  "minuto_liberacion debe copiarse");
        comprobar(vista[i].reservado == origen.arbitros[i].reservado,
                  "reservado debe copiarse");
    }
}

} // namespace

int main() {
    // Garantías de layout de las piezas que la arena empaqueta.
    static_assert(sizeof(CabeceraNodo) == 24);
    static_assert(sizeof(EstadoArbitro) == 4);
    static_assert(sizeof(Estado) == 2400);
    static_assert(kMaxArbitros == 600);

    // Fórmula 24 + 4n con redondeo a 8 B.
    comprobar(Arena::calcular_tam_bloque(1) == 32, "n = 1 debe dar 32 B");
    comprobar(Arena::calcular_tam_bloque(2) == 32, "n = 2 debe dar 32 B");
    comprobar(Arena::calcular_tam_bloque(5) == 48, "n = 5 debe dar 48 B");
    comprobar(Arena::calcular_tam_bloque(300) == 1224, "n = 300 debe dar 1224 B");
    comprobar(Arena::calcular_tam_bloque(600) == 2424, "n = 600 debe dar 2424 B");

    // Estado inicial, día intermedio y día máximo.
    for (std::size_t n : {std::size_t{1}, std::size_t{5}, std::size_t{300},
                          std::size_t{600}}) {
        probar_tamano_y_copia(n);
    }

    // Dos estados que solo difieren en la cola [n, 600) producen el mismo nodo:
    // la parte no significativa se ignora.
    {
        constexpr std::size_t n = 7;
        Estado a{};
        preparar_estado(a, n);
        for (std::size_t i = 0; i < n; ++i) {
            a.arbitros[i].sede_id = static_cast<std::uint32_t>(i + 1);
        }

        Estado b = a; // misma parte significativa...
        for (std::size_t i = n; i < kMaxArbitros; ++i) {
            // ...y cola distinta.
            b.arbitros[i].sede_id = 1023;
            b.arbitros[i].minuto_liberacion = 2047;
        }

        Arena arena(n);
        const std::uint32_t nodo_a = arena.crear(a, CabeceraNodo{});
        const std::uint32_t nodo_b = arena.crear(b, CabeceraNodo{});

        const std::span<const EstadoArbitro> vista_a = arena.estado(nodo_a);
        const std::span<const EstadoArbitro> vista_b = arena.estado(nodo_b);
        comprobar(vista_a.size() == n && vista_b.size() == n,
                  "ambos nodos deben medir n");
        for (std::size_t i = 0; i < n; ++i) {
            comprobar(vista_a[i].sede_id == vista_b[i].sede_id,
                      "la cola del Estado no debe influir en el nodo");
            comprobar(vista_a[i].minuto_liberacion ==
                          vista_b[i].minuto_liberacion,
                      "la cola del Estado no debe influir en el nodo");
        }
    }

    // Crear desde un span más largo que n_arbitros también copia solo el prefijo.
    {
        std::array<EstadoArbitro, 10> buffer{};
        buffer[2].minuto_liberacion = 500;

        Arena arena(3);
        const std::uint32_t nodo =
            arena.crear(std::span<const EstadoArbitro>(buffer), CabeceraNodo{});

        comprobar(arena.estado(nodo).size() == 3,
                  "el span del nodo debe medir n, no lo que mida el origen");
        comprobar(arena.estado(nodo)[2].minuto_liberacion == 500,
                  "el prefijo si debe copiarse");
    }

    // Direcciones estables: crear muchos nodos (y con ello nuevos bloques) no
    // invalida los punteros de los nodos anteriores.
    {
        Arena arena(300);

        Estado inicial{};
        inicial.arbitros[0].sede_id = 99;
        inicial.arbitros[0].minuto_liberacion = 123;

        const std::uint32_t raiz = arena.crear(inicial, CabeceraNodo{});
        CabeceraNodo* cabecera_raiz = &arena.cabecera(raiz);
        EstadoArbitro* estado_raiz = arena.estado(raiz).data();

        const std::size_t por_bloque = Arena::kBytesPorBloque / arena.tam_bloque();
        const std::size_t total = por_bloque * 20; // fuerza unos 20 bloques
        arena.reservar(total + 1);
        comprobar(arena.capacidad() >= total + 1,
                  "reservar debe garantizar capacidad");

        for (std::size_t i = 0; i < total; ++i) {
            arena.crear(inicial, CabeceraNodo{});
        }

        comprobar(arena.nodos_creados() == total + 1,
                  "deben existir todos los nodos creados");
        comprobar(&arena.cabecera(raiz) == cabecera_raiz,
                  "la cabecera del nodo no debe moverse");
        comprobar(arena.estado(raiz).data() == estado_raiz,
                  "el estado del nodo no debe moverse");
        comprobar(arena.estado(raiz)[0].sede_id == 99,
                  "el nodo debe conservar su contenido");
        comprobar(arena.estado(raiz)[0].minuto_liberacion == 123,
                  "el nodo debe conservar su contenido");
        comprobar(arena.capacidad() >= arena.nodos_creados(),
                  "la capacidad debe cubrir los nodos creados");
        comprobar(arena.memoria_reservada() ==
                      arena.capacidad() * arena.tam_bloque(),
                  "la memoria reservada debe ser capacidad * tam_bloque");
    }

    // La cabecera por defecto apunta a "sin padre" y no arrastra basura.
    {
        Arena arena(10);
        const std::uint32_t nodo = arena.crear(Estado{}, CabeceraNodo{});
        comprobar(arena.cabecera(nodo).padre == kSinNodo,
                  "la raiz debe nacer sin padre");
        comprobar(arena.cabecera(nodo).coste_g == 0,
                  "la raiz debe nacer con coste 0");
        comprobar(arena.cabecera(nodo).nivel == 0,
                  "la raiz debe nacer en el nivel 0");
    }

    if (fallos == 0) {
        std::cout << "test_arena: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
