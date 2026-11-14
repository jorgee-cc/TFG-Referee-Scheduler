Pasemos a programar la funcion objetivo. Finalmente queda como €_ganados + k €_tiempo Y concretamente como coste = coste_padre + k €tiempo. El dinero no se suma y pasa a ser una penalizacion sumando unicamente los tiempos y si se deja una plaza vacia lo que hubiese ganado el arbitro es el coste que se acomula. Va a estar todo en centimos y k se va a ajustar para 8€/h, 14 centimos el minuto.

pseudocodigo

int64_t coste_transicion = k_epsilon; // Ej: 1 céntimo simbólico

// 1. Cálculo del viaje (Matriz O(1) de la fase 5a)
int tiempo_viaje = matriz_distancias[estado_arbitro.sede_id][plaza.sede_id];
int minuto_llegada = estado_arbitro.minuto_liberacion + tiempo_viaje;
int tiempo_espera = plaza.minuto_inicio - minuto_llegada;

// 2. Penalización por tiempo consumido (k constante)
if (estado_arbitro.sede_id == arbitro.sede_domicilio) {
// Es el primer partido: el tiempo esperando en casa es gratis.
// Solo se penaliza el tiempo estricto de conducción.
coste_transicion += tiempo_viaje _ k_centimos;
} else {
// Ya está en ruta: se penaliza tanto el viaje como el tiempo muerto de espera.
coste_transicion += (tiempo_viaje + tiempo_espera) _ k_centimos;
}

Decisiones de diseño posteriores:

1. Transición por Asignar Plaza
   El coste sumado representa exclusivamente el desgaste y el tiempo, asumiendo temporalmente que la jornada termina ahí:Épsilon: Un valor mínimo estático (ej. 1) añadido siempre para asegurar que la ruta encarece y evitar mesetas de coste 0.Tiempo consumido: (Minutos de viaje desde la sede actual del árbitro hasta la del partido + minutos de espera hasta que arranque el partido) $\times$ $k$.Ajuste del viaje de retorno: (Minutos de viaje desde la nueva sede hasta su casa $-$ minutos de viaje desde la sede anterior hasta su casa) $\times$ $k$.(En esta rama no se suma ni se resta el valor económico de la plaza).2. Transición por Dejar Plaza Vacía
   El coste asume la penalización económica por abandono:Si la plaza tiene Rol::Principal: Transición prohibida. El generador simplemente no crea este nodo hijo. Si la plaza tiene rol secundario: Se suma a coste_g el valor total de la retribución perdida (convertida a céntimos enteros).
