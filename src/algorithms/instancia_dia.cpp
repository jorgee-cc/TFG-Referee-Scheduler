// Implementación de `InstanciaDia`: solo los accesores que comprueban los
// índices. La construcción de los datos (fases 1-5) vive en
// `construir_instancia.cpp`.
//
// `viaje` no es simétrico: devuelve la celda `[desde][hasta]` de la matriz, que
// la fase 3 rellena con los tiempos reales de OSRM en cada sentido.
//
// Los accesores de candidatos (`n_candidatos`, `candidatos_de`) solo tienen
// sentido después de la fase 5 y exigen con `assert` que el CSR esté completo:
// los dos arrays se llenan juntos, así que un tamaño de `offset_candidatos`
// distinto de `n_plazas() + 1` significa que la instancia se está usando a
// medio construir.

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

    std::size_t InstanciaDia::n_plazas() const noexcept
    {
        return plazas.size();
    }

    std::uint32_t InstanciaDia::n_candidatos(std::size_t plaza) const
    {
        assert(plaza < n_plazas() && "plaza fuera de rango");
        assert(offset_candidatos.size() == n_plazas() + 1 &&
               "las listas de candidatos (fase 5) no estan construidas");

        return offset_candidatos[plaza + 1] - offset_candidatos[plaza];
    }

    std::span<const std::uint32_t>
    InstanciaDia::candidatos_de(std::size_t plaza) const
    {
        assert(plaza < n_plazas() && "plaza fuera de rango");
        assert(offset_candidatos.size() == n_plazas() + 1 &&
               "las listas de candidatos (fase 5) no estan construidas");
        // La última frontera es el tamaño del array por construcción, así que
        // `inicio + longitud` nunca se sale de `candidatos`.
        assert(offset_candidatos.back() == candidatos.size() &&
               "el CSR de candidatos esta mal formado");

        const std::uint32_t inicio = offset_candidatos[plaza];
        const std::uint32_t longitud = n_candidatos(plaza);

        // Se construye el span desde el vector completo y se recorta con
        // `subspan`: así una lista vacía con `candidatos` vacío no hace
        // aritmética sobre un puntero nulo (la suma de un desplazamiento sobre
        // `data()` de un vector vacío no está definida por el estándar).
        return std::span<const std::uint32_t>(candidatos)
            .subspan(inicio, longitud);
    }

    std::size_t InstanciaDia::n_candidatos_totales() const noexcept
    {
        return candidatos.size();
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
