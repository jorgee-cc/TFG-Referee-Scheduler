// Pruebas de `algorithms::objetivo.hpp`: conversión de euros a céntimos, primer
// desplazamiento desde el domicilio (puede salir antes de su disponibilidad),
// espera en ruta, ajuste del viaje de retorno (positivo y negativo), llegada
// tarde como transición infactible y coste de dejar plazas vacías. Es el
// ejecutable `referee_scheduler_objetivo_tests` de CTest.

#include <cstdint>
#include <iostream>
#include <optional>

#include "referee_scheduler/algorithms/objetivo.hpp"

namespace {

using referee_scheduler::algorithms::EstadoArbitro;
using referee_scheduler::algorithms::ParametrosObjetivo;
using referee_scheduler::algorithms::a_centimos;
using referee_scheduler::algorithms::coste_asignar;
using referee_scheduler::algorithms::coste_dejar_vacia;
using referee_scheduler::domain::Rol;

int fallos = 0;

/// Registra un fallo sin abortar: así se ven todos los problemas de una vez.
void comprobar(bool condicion, const char* mensaje) {
    if (!condicion) {
        std::cerr << "FALLO: " << mensaje << '\n';
        ++fallos;
    }
}

/// Comprueba que una transición factible devuelve exactamente `esperado`.
void comprobar_coste(const std::optional<std::int64_t>& obtenido,
                     std::int64_t esperado,
                     const char* mensaje) {
    if (!obtenido.has_value()) {
        std::cerr << "FALLO: " << mensaje << " (devolvio nullopt)\n";
        ++fallos;
        return;
    }
    if (*obtenido != esperado) {
        std::cerr << "FALLO: " << mensaje << " (esperado " << esperado
                  << ", obtenido " << *obtenido << ")\n";
        ++fallos;
    }
}

/// Construye el registro de 4 B de un árbitro en una sede y minuto dados.
EstadoArbitro arbitro_en(int sede_id, int minuto_liberacion) {
    EstadoArbitro arbitro{};
    arbitro.sede_id = static_cast<std::uint32_t>(sede_id);
    arbitro.minuto_liberacion = static_cast<std::uint32_t>(minuto_liberacion);
    return arbitro;
}

} // namespace

int main() {
    const ParametrosObjetivo por_defecto{}; // k = 14 c/min, epsilon = 1 c

    // --- Conversión euros -> céntimos -------------------------------------
    comprobar(a_centimos(8.0) == 800, "8 euros deben ser 800 centimos");
    comprobar(a_centimos(18.5) == 1850, "18,5 euros deben ser 1850 centimos");
    comprobar(a_centimos(12.345) == 1235, "12,345 euros deben redondear a 1235 centimos");
    comprobar(a_centimos(0.0) == 0, "0 euros deben ser 0 centimos");
    comprobar(a_centimos(0.004) == 0, "0,004 euros deben redondear a 0 centimos");
    comprobar(a_centimos(0.005) == 1, "0,005 euros deben redondear a 1 centimo");
    // k = 14 c/min son 14 * 60 = 840 c/h = 8,4 EUR/h.
    comprobar(a_centimos(8.4) == referee_scheduler::algorithms::kCentimosPorMinuto * 60,
              "la conversion debe ser coherente con k = 14 c/min");

    // --- Primer desplazamiento: puede salir antes de su disponibilidad ------
    // Arbitro en su domicilio (7), disponible a las 540 (9:00). Viaje de 10 min,
    // partido a las 610. Sale de casa a las 600 y llega justo: no hay espera.
    // Coste = 1 + 10*14 + (12-0)*14 = 309.
    comprobar_coste(
        coste_asignar(por_defecto, arbitro_en(7, 540), 7, 610, 10, 12, 0),
        309, "primer desplazamiento: solo se cobra el viaje y el retorno");

    // El viaje puede empezar antes de la disponibilidad: disponible a las 9:00,
    // dos horas de viaje y partido a las 9:00. Sale a las 7:00, asi que es
    // factible y no hay espera. Coste = 1 + 120*14 + (120-0)*14 = 3361.
    comprobar_coste(
        coste_asignar(por_defecto, arbitro_en(7, 540), 7, 540, 120, 120, 0),
        3361, "el primer partido se alcanza aunque el viaje empiece antes de la disponibilidad");

    // Pero no puede arbitrar antes de estar disponible: a las 8:59 no vale.
    comprobar(
        !coste_asignar(por_defecto, arbitro_en(7, 540), 7, 539, 10, 0, 0).has_value(),
        "no puede empezar un partido antes de su disponibilidad");

    // Un partido muy posterior: sale de casa mas tarde, sin espera.
    // Coste = 1 + 10*14 + (10-0)*14 = 281.
    comprobar_coste(
        coste_asignar(por_defecto, arbitro_en(7, 540), 7, 1080, 10, 10, 0),
        281, "un partido lejano no genera espera: se sale de casa a tiempo");

    // Contraste: si ya esta en una sede (no en su domicilio), el mismo viaje de
    // 120 min desde las 9:00 llega a las 11:00 y no vale para un partido a las
    // 10:00. Es la diferencia entre poder salir antes y estar ya en ruta.
    comprobar(
        !coste_asignar(por_defecto, arbitro_en(3, 540), 7, 600, 120, 0, 0).has_value(),
        "desde una sede el viaje empieza al quedar libre y puede llegar tarde");

    // --- En ruta: la espera sí es tiempo muerto ----------------------------
    // Sede actual 3 (no es su domicilio), libre a las 600. Viaje 10, inicio
    // 630 -> espera 20. Retorno nuevo 12, anterior 5.
    // Coste = 1 + (10+20)*14 + (12-5)*14 = 519.
    comprobar_coste(
        coste_asignar(por_defecto, arbitro_en(3, 600), 7, 630, 10, 12, 5),
        519, "en ruta: se cobra viaje, espera y ajuste de retorno");

    // --- Ajuste de retorno negativo ---------------------------------------
    // La nueva sede (retorno 2) está mas cerca de casa que la anterior (9).
    // Coste = 1 + (4+6)*14 + (2-9)*14 = 43. Sigue siendo >= epsilon.
    {
        const auto coste = coste_asignar(por_defecto, arbitro_en(3, 0), 7, 10, 4, 2, 9);
        comprobar_coste(coste, 43, "el ajuste de retorno puede ser negativo");
        comprobar(coste.has_value() && *coste >= referee_scheduler::algorithms::kEpsilonCentimos,
                  "ninguna transicion factible puede bajar del epsilon");
    }

    // --- Llegada tarde -----------------------------------------------------
    comprobar(!coste_asignar(por_defecto, arbitro_en(3, 600), 7, 609, 10, 0, 0).has_value(),
              "llegar despues del inicio debe devolver nullopt");

    // Llegar justo a la hora es factible y la espera es 0.
    // Coste = 1 + 10*14 + 0 = 141.
    comprobar_coste(coste_asignar(por_defecto, arbitro_en(3, 600), 7, 610, 10, 0, 0),
                    141, "llegar justo a la hora debe ser factible");

    // --- Sin viaje ni espera: queda solo el epsilon ------------------------
    comprobar_coste(coste_asignar(por_defecto, arbitro_en(7, 600), 7, 600, 0, 0, 0),
                    referee_scheduler::algorithms::kEpsilonCentimos,
                    "sin tiempo consumido el coste es el epsilon");

    // --- Parametros personalizados ----------------------------------------
    {
        ParametrosObjetivo sin_k{};
        sin_k.centimos_por_minuto = 0;
        comprobar_coste(coste_asignar(sin_k, arbitro_en(7, 540), 7, 900, 10, 30, 0),
                        1, "con k = 0 solo queda el epsilon");

        ParametrosObjetivo con_k{};
        con_k.centimos_por_minuto = 10;
        con_k.epsilon_centimos = 0;
        // Primer partido: 1 + (5+0)*10 + (5-0)*10 con epsilon 0.
        comprobar_coste(coste_asignar(con_k, arbitro_en(7, 540), 7, 600, 5, 5, 0),
                        100, "k y epsilon personalizados deben usarse");
    }

    // --- Plazas sin cubrir -------------------------------------------------
    comprobar(coste_dejar_vacia(Rol::Principal, 2500) == std::nullopt,
              "una plaza principal no puede quedar vacia");

    comprobar_coste(coste_dejar_vacia(Rol::Asistente, 1850), 1850,
                    "una plaza secundaria cuesta su retribucion");
    comprobar_coste(coste_dejar_vacia(Rol::Anotador, 900), 900,
                    "el rol anotador tambien puede abandonarse");
    comprobar_coste(coste_dejar_vacia(Rol::JuezLinea, 1200), 1200,
                    "el rol juez de linea tambien puede abandonarse");
    comprobar_coste(coste_dejar_vacia(Rol::Asistente, 0), 0,
                    "una plaza sin retribucion no suma coste");

    if (fallos == 0) {
        std::cout << "test_objetivo: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
