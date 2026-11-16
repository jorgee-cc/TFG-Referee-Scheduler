// Pruebas de `algorithms::ListaAbierta` (cubos de la frontera de Dijkstra):
// orden de extracción, empates LIFO, base que solo avanza, cubos de anchura
// mayor que uno y, sobre todo, una prueba de propiedad contra
// `std::priority_queue` con secuencias aleatorias de claves.
// Es el ejecutable `referee_scheduler_abierta_tests` de CTest.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <queue>
#include <random>
#include <utility>
#include <vector>

#include "referee_scheduler/algorithms/abierta.hpp"

namespace
{

    using referee_scheduler::algorithms::ListaAbierta;

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

    // ---------------------------------------------------------------------
    // Orden de extracción y empates
    // ---------------------------------------------------------------------
    void probar_orden_basico()
    {
        ListaAbierta abierta;
        comprobar(abierta.vacia(), "una lista nueva esta vacia");
        comprobar(abierta.size() == 0, "una lista nueva no tiene nodos");
        comprobar(abierta.anchura() == 1, "la anchura por defecto es 1 centimo");

        // Las claves se insertan en orden no decreciente, que es la precondición
        // de la estructura (Dijkstra la garantiza: el coste de un hijo nunca es
        // menor que el de su padre).
        abierta.insertar(10, 3);
        abierta.insertar(11, 3); // misma clave que el anterior
        abierta.insertar(12, 4);
        abierta.insertar(13, 5);
        abierta.insertar(14, 7);

        comprobar(abierta.size() == 5, "deben encolarse cinco nodos");
        comprobar(abierta.clave_min() == 3, "la clave minima es 3");

        // Dentro de un cubo se extrae LIFO: primero el ultimo insertado.
        comprobar(abierta.extraer_min() == 11,
                  "el empate del cubo se resuelve LIFO (ultimo insertado)");
        comprobar(abierta.extraer_min() == 10, "el segundo empate es el primero insertado");
        comprobar(abierta.extraer_min() == 12, "despues sale la clave 4");
        comprobar(abierta.extraer_min() == 13, "despues sale la clave 5");
        comprobar(abierta.extraer_min() == 14, "por ultimo sale la clave 7");

        comprobar(abierta.vacia(), "la lista queda vacia");
        comprobar(abierta.size() == 0, "el tamano vuelve a cero");
    }

    // ---------------------------------------------------------------------
    // Base y crecimiento: claves lejos del cero
    // ---------------------------------------------------------------------
    void probar_base_y_crecimiento()
    {
        ListaAbierta abierta;

        // La base se fija con la primera inserción, no en 0: así una clave de un
        // millón no materializa un millón de cubos vacíos.
        abierta.insertar(0, 1'000'000);
        comprobar(abierta.base() == 1'000'000,
                  "la base es la clave de la primera insercion");
        comprobar(abierta.n_cubos() < 1'000'000,
                  "la base evita materializar los cubos por debajo de la clave");

        const std::size_t cubos_iniciales = abierta.n_cubos();
        abierta.insertar(1, 1'001'000);
        comprobar(abierta.n_cubos() > cubos_iniciales,
                  "un salto grande de clave obliga a crecer");
        comprobar(abierta.n_cubos() >= 1'001,
                  "debe haber un cubo por centimo hasta la clave insertada");
        comprobar(abierta.size() == 2, "los dos nodos siguen encolados");
        comprobar(abierta.clave_min() == 1'000'000, "sigue mandando la clave menor");

        comprobar(abierta.extraer_min() == 0, "sale primero el nodo de la clave menor");
        comprobar(abierta.clave_min() == 1'001'000, "el cursor avanza hasta la otra clave");
        comprobar(abierta.extraer_min() == 1, "sale despues el nodo de la clave mayor");

        // El número de cubos nunca encoge, y la memoria es una métrica positiva.
        comprobar(abierta.n_cubos() >= 1'001, "los cubos no se liberan al vaciarse");
        comprobar(abierta.memoria_reservada() > 0, "la lista compromete memoria");
    }

    // ---------------------------------------------------------------------
    // Cubos de anchura mayor que uno (cota inferior, no clave exacta)
    // ---------------------------------------------------------------------
    void probar_anchura()
    {
        ListaAbierta abierta(100);
        comprobar(abierta.anchura() == 100, "la anchura configurada se conserva");

        // La base se alinea hacia abajo al múltiplo de la anchura: la primera
        // clave 250 cae en el cubo que empieza en 200.
        abierta.insertar(0, 250);
        comprobar(abierta.base() == 200, "la base se alinea al multiplo de la anchura");

        abierta.insertar(1, 240); // mismo cubo que la anterior
        abierta.insertar(2, 350); // cubo siguiente

        // 240 y 250 comparten cubo: el mínimo es la cota inferior 200, que nunca
        // sobreestima la clave real.
        comprobar(abierta.clave_min() == 200,
                  "con anchura mayor la clave minima es la cota inferior del cubo");
        comprobar(abierta.extraer_min() == 1,
                  "sale primero la clave 240 (misma cubeta, LIFO)");
        comprobar(abierta.extraer_min() == 0, "sale despues la clave 250");
        comprobar(abierta.clave_min() == 300, "ahora el primer cubo es el de 350");
        comprobar(abierta.extraer_min() == 2, "sale la clave 350");
        comprobar(abierta.vacia(), "la lista queda vacia");
    }

    // ---------------------------------------------------------------------
    // Propiedad: mismo orden de claves que un heap
    // ---------------------------------------------------------------------
    void probar_contra_priority_queue()
    {
        std::mt19937 generador(20261113);
        ListaAbierta abierta;
        std::priority_queue<std::int64_t, std::vector<std::int64_t>,
                            std::greater<std::int64_t>>
            oraculo;

        std::int64_t clave = 0;
        std::uint32_t siguiente_nodo = 0;
        bool mismo_orden = true;
        std::size_t extraidos = 0;

        // Secuencia pseudoaleatoria de inserciones con claves no decrecientes
        // (la precondición de la estructura) y extracciones intercaladas.
        for (int paso = 0; paso < 20000; ++paso)
        {
            const bool encolar = oraculo.empty() || (generador() % 100) < 60;

            if (encolar)
            {
                clave += static_cast<std::int64_t>(generador() % 4);
                abierta.insertar(siguiente_nodo++, clave);
                oraculo.push(clave);
            }
            else
            {
                const std::int64_t esperada = oraculo.top();
                oraculo.pop();
                if (abierta.clave_min() != esperada)
                {
                    mismo_orden = false;
                }
                abierta.extraer_min();
                ++extraidos;
            }
        }

        while (!oraculo.empty())
        {
            const std::int64_t esperada = oraculo.top();
            oraculo.pop();
            if (abierta.clave_min() != esperada)
            {
                mismo_orden = false;
            }
            abierta.extraer_min();
            ++extraidos;
        }

        comprobar(mismo_orden,
                  "la lista abierta debe extraer las claves en el mismo orden que el heap");
        comprobar(abierta.vacia(), "tras vaciar el oraculo la lista queda vacia");
        comprobar(extraidos > 0, "la prueba debe haber extraido nodos");
    }

    // ---------------------------------------------------------------------
    // Traslado: mover la estructura no pierde los nodos
    // ---------------------------------------------------------------------
    void probar_movimiento()
    {
        ListaAbierta original;
        original.insertar(7, 3);
        original.insertar(9, 8);

        ListaAbierta movida = std::move(original);
        comprobar(movida.size() == 2, "el movimiento conserva los nodos");
        comprobar(movida.extraer_min() == 7, "el movimiento conserva el orden");
        comprobar(movida.extraer_min() == 9, "el movimiento conserva el segundo nodo");
    }

} // namespace

int main()
{
    probar_orden_basico();
    probar_base_y_crecimiento();
    probar_anchura();
    probar_contra_priority_queue();
    probar_movimiento();

    if (fallos == 0)
    {
        std::cout << "test_abierta: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
