// Prueba de humo del estado empaquetado de la búsqueda.
// Comprueba las garantías de layout (`EstadoArbitro` de 4 B, `Estado` de
// 2400 B y capacidad de 600 árbitros), los valores límite de cada campo y que
// copiar el estado con `memcpy` conserve el contenido. La arena y la función
// objetivo tienen su propio ejecutable de pruebas.

#include <cstddef>
#include <cstring>
#include <iostream>

#include "referee_scheduler/algorithms/estado.hpp"

namespace {

using referee_scheduler::algorithms::Estado;
using referee_scheduler::algorithms::EstadoArbitro;

int fallos = 0;

/// Registra un fallo sin abortar: así se ven todos los problemas de una vez.
void comprobar(bool condicion, const char* mensaje) {
    if (!condicion) {
        std::cerr << "FALLO: " << mensaje << '\n';
        ++fallos;
    }
}

} // namespace

int main() {
    // Garantías de layout: el estado debe seguir siendo denso y copiable.
    static_assert(sizeof(EstadoArbitro) == 4);
    static_assert(sizeof(Estado) == 2400);
    static_assert(referee_scheduler::algorithms::kMaxArbitros == 600);

    // El estado nace a cero: ningún árbitro tiene sede, tiempo ni reserva.
    Estado estado{};
    for (const EstadoArbitro& arbitro : estado.arbitros) {
        comprobar(arbitro.sede_id == 0, "sede_id inicial debe ser 0");
        comprobar(arbitro.minuto_liberacion == 0, "minuto_liberacion inicial debe ser 0");
        comprobar(arbitro.reservado == 0, "reservado inicial debe ser 0");
    }

    // Valores límite de cada campo.
    estado.arbitros[0].sede_id = 1023;
    estado.arbitros[0].minuto_liberacion = 2047;
    estado.arbitros[0].reservado = 2047;

    comprobar(estado.arbitros[0].sede_id == 1023, "sede_id debe admitir 1023");
    comprobar(estado.arbitros[0].minuto_liberacion == 2047,
              "minuto_liberacion debe admitir 2047");
    comprobar(estado.arbitros[0].reservado == 2047, "reservado debe admitir 2047");

    // Un día completo (1440 minutos) debe caber en los 11 bits del minuto.
    comprobar(1440 <= 2047, "un día de 1440 minutos debe caber en minuto_liberacion");

    // La copia es trivial (memcpy) y conserva el contenido.
    Estado copia{};
    std::memcpy(&copia, &estado, sizeof(Estado));

    comprobar(copia.arbitros[0].sede_id == 1023, "la copia debe conservar sede_id");
    comprobar(copia.arbitros[0].minuto_liberacion == 2047,
              "la copia debe conservar minuto_liberacion");
    comprobar(copia.arbitros[0].reservado == 2047, "la copia debe conservar reservado");

    if (fallos == 0) {
        std::cout << "test_smoke: OK\n";
    }
    return fallos == 0 ? 0 : 1;
}
