# Mejoras prácticas

Ideas que no se implementan todavía porque dependen de piezas que aún no existen
(matriz de distancias, generador de transiciones) o porque conviene medirlas
antes de complicar la búsqueda.

## Tiempos largos: ida y vuelta a casa sin contarlos como tiempo muerto

### Situación actual

La función objetivo (`algorithms/objetivo.hpp`) cobra todos los minutos que el
árbitro pasa esperando entre dos partidos como tiempo muerto:

```
coste = ε + (tiempo_viaje + espera) · k        // si ya no está en casa
```

Solo la espera en el domicilio es gratis (primer partido del día). Esto es
razonable para huecos cortos, pero penaliza una situación real: entre dos
partidos puede haber un hueco de varias horas. El árbitro no tiene por qué
quedarse en la sede esperando; puede volver a casa, descansar y regresar.

Ejemplo: termina a las 12:00 en una sede a 20 min de casa y su siguiente
partido es a las 17:00 en una sede a 15 min de casa. Con el modelo actual se
cobran 300 minutos de espera aunque el árbitro esté en su casa. Lo físico es
conducir 20 min de ida, descansar y conducir 15 min de vuelta: 35 minutos de
trabajo, no 300 de tiempo muerto.

### Regla propuesta

Si el hueco da tiempo a **ir a casa + volver a la siguiente sede + al menos una
hora en casa**, la transición puede cobrarse como ida y vuelta en lugar de como
espera:

```
llegada_a_casa = minuto_liberacion + d(sede_actual, domicilio)
espera_en_casa = minuto_inicio - (llegada_a_casa + d(domicilio, sede_plaza))

si espera_en_casa >= 60:
    coste = ε
          + (d(domicilio, sede_plaza)) · k          // vuelta al partido
          + d(sede_plaza, domicilio) · k            // retorno previsto (ajuste)
```

No se cobra la hora (ni el resto) que pasa en casa: los únicos minutos que
cuentan son los de conducción. La espera en casa ya es gratis en la fórmula
actual, así que basta con evaluar la transición **como si el árbitro estuviera
en su domicilio**:

```cpp
// Variante "por casa" de la misma transición:
EstadoArbitro tras_casa = estado_actual;
tras_casa.sede_id = domicilio;
tras_casa.minuto_liberacion = liberacion + d(sede_actual, domicilio);

coste = coste_asignar(parametros, tras_casa, domicilio,
                      minuto_inicio, d(domicilio, sede_plaza),
                      d(sede_plaza, domicilio), /*retorno_anterior=*/0);
```

Es decir, **no hace falta un estado nuevo ni un bit de historial**: la misma
`coste_asignar` sirve, solo cambia la sede y el minuto de liberación con los
que se llama.

### Cómo integrarlo en el generador de transiciones

Para cada plaza candidata se calculan dos costes:

1. **Directa**: la transición normal desde la sede actual.
2. **Por casa**: la variante anterior, solo si la sede actual no es ya el
   domicilio y se cumple `espera_en_casa >= 60`.

El hijo que se crea es el mismo en los dos casos (misma sede final, mismo
`minuto_liberacion` tras el partido), así que basta con quedarse con el menor
de los dos costes. No se duplican nodos ni se añade una transición "volver a
casa" que infle el factor de ramificación: son dos lecturas más de la matriz de
distancias por candidato.

### Por qué es correcto

- El ajuste de retorno es prospectivo: cada asignación ya cobra
  `d(sede_nueva, domicilio) - d(sede_anterior, domicilio)`. Si la sede anterior
  es la actual y la nueva es el domicilio, esa ida ya está pagada; por eso la
  variante "por casa" empieza en el domicilio con `retorno_anterior = 0` y no
  cobra dos veces el mismo trayecto.
- El ajuste de la transición directa ya incluía la ida a casa como retorno
  previsto. Al elegir la variante por casa, ese importe se convierte en el
  viaje real de ida, y la vuelta al partido se cobra como viaje normal.
- Con matriz métrica, ambos costes siguen siendo `>= ε`, así que la arista no
  se vuelve negativa y Dijkstra sigue siendo válido.

### Parámetros y decisiones pendientes

- **Hora mínima en casa** (60 min propuestos): valor de confort, ajustable. Si
  se pone a 0, el modelo permite ir y volver sin descanso; si es muy alto, casi
  nunca compensa.
- **Espera en casa**: se deja a coste 0. Alternativa: cobrar un épsilon o una
  fracción de `k` para no premiar huecos artificialmente largos.
- **¿Y si el hueco es aún mayor?** El modelo no distingue entre 1 h y 5 h en
  casa; ambas cuestan lo mismo. Si se quisiera, habría que añadir un coste por
  hora de disponibilidad, lo que rompería la linealidad actual.
- **Última hora del día**: el ajuste de retorno asume que la jornada termina
  en la última sede. La variante por casa no cambia eso: el último partido
  sigue pagando su vuelta a casa.
- **Matriz de distancias**: todo esto necesita `d(sede, sede)` (paso 5d). Sin
  ella no se puede medir si compensa.

### Coste de implementación

Bajo: la fórmula no cambia, solo se llama dos veces con estados de partida
distintos y se toma el mínimo. Requiere únicamente:

1. La matriz de distancias.
2. El generador de transiciones (paso 3).
3. Una constante `kMinutosMinimosEnCasa = 60` y dos lecturas extra de matriz.
