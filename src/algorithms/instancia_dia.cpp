// Implementación de `InstanciaDia`: solo los accesores que comprueban los
// índices. La construcción de los datos (fases 1-5) vive en
// `construir_instancia.cpp`.
//
// `viaje` no es simétrico: devuelve la celda `[desde][hasta]` de la matriz, que
// la fase 3 rellena con los tiempos reales de OSRM en cada sentido.

#include "referee_scheduler/algorithms/instancia_dia.hpp"

#include <cassert>

namespace referee_scheduler::algorithms
{

    std::size_t InstanciaDia::n_arbitros() const noexcept
    {
        return arbitros.size();
    }

    std::size_t InstanciaDia::n_sedes() const noexcept
    {
        return sede_global.size();
    }

    Minutos InstanciaDia::viaje(std::size_t desde, std::size_t hasta) const
    {
        assert(desde < n_sedes() && "sede de origen fuera de rango");
        assert(hasta < n_sedes() && "sede de destino fuera de rango");
        assert(distancias.size() == n_sedes() * n_sedes() &&
               "la matriz de distancias no esta completa");

        return distancias[desde * n_sedes() + hasta];
    }

} // namespace referee_scheduler::algorithms
