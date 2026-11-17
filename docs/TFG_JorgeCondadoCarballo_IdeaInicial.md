# Finalidad del documento

Este documento es unicamente la idea inicial del TFG sin entrar en detalles. No es un contrato que hay que cumplir y no hay que leerlo para tomar decisiones del codigo es unicamente como apoyo para saber que ha cambiado respecto al inicio
##############################################

**TRABAJO FIN DE GRADO**

**Asignación Óptima de Árbitros a Partidos** _Maximización de la Eficiencia Económica del Colectivo Arbitral_

**Autor:**

Jorge Condado Carballo  
**1\. Descripción del Problema**

El núcleo de este Trabajo de Fin de Grado consiste en la asignación óptima de árbitros a partidos en sus respectivas sedes.

En una liga deportiva, los partidos se programan en distintos horarios y en múltiples ubicaciones distribuidas geográficamente. Para que estos encuentros puedan celebrarse, es necesario asignar uno o varios árbitros a cada partido. Cada árbitro debe desplazarse desde un punto de origen, su domicilio, hasta el siguiente encuentro, lo que implica un tiempo de desplazamiento variable.

Este tiempo de desplazamiento depende de diversos factores:

• La distancia entre ubicaciones, que determina el recorrido mínimo necesario.

• El medio de transporte utilizado, ya sea público o privado, que influye en la velocidad y flexibilidad del trayecto.

• Las condiciones del tráfico, que pueden provocar variaciones significativas en la duración real del desplazamiento.

Además de los tiempos de desplazamiento, existen periodos intermedios entre partidos denominados tiempos muertos, durante los cuales el árbitro no puede aprovechar ese tiempo de forma productiva ni recibe compensación económica, quedando condicionado por la planificación de los encuentros.

Por tanto, el problema no se limita únicamente a la asignación de árbitros a partidos, sino que también implica considerar la eficiencia global de dicha asignación. En la práctica, los árbitros perciben ingresos únicamente por el tiempo efectivo de arbitraje, mientras que los tiempos de desplazamiento y espera no son remunerados.

Este problema se enmarca dentro de la familia del Referee Assignment Problem (RAP), un problema ampliamente estudiado en investigación operativa y planificación deportiva. Se ha tomado como referencia el trabajo de Vallespí Soro (2019), realizado en colaboración con la Federación Holandesa de Fútbol (KNVB), que aborda el RAP mediante programación lineal entera y búsqueda local. El presente TFG se diferencia de dicho trabajo al incorporar un objetivo centrado en la eficiencia económica individual de los árbitros, así como restricciones adicionales relativas a los tiempos muertos, el transporte público y los conflictos de interés.

**2\. Objetivo**

El objetivo de este trabajo es maximizar la eficiencia económica del colectivo de árbitros. Dicha eficiencia se define, para cada árbitro, como el cociente entre los ingresos obtenidos y el tiempo total invertido, contabilizado desde la salida del domicilio hasta la llegada al mismo, incluyendo el tiempo de arbitraje, los tiempos de desplazamiento y los tiempos muertos.  
El trabajo se considerará exitoso si el sistema produce asignaciones que mejoren, de forma demostrable y cuantificable, las realizadas manualmente por la federación.

El problema se resolverá como un problema de búsqueda, con un algoritmo por determinar y programado en C++.

**3\. Función Objetivo**

Se han evaluado cuatro alternativas de formulación para la función objetivo:

**Opción A — Suma de eficiencias individuales**

������������������  
������  
∑

��ϵ��  
��������������

El problema de esta formulación es que un árbitro con tiempo total muy pequeño dispara su eficiencia individual. El modelo podría preferir dejar árbitros sin asignar antes que asignarles partidos poco rentables o asignar a muchos árbitros muy pocos partidos y a otros muchos más.

**Opción B — Media de eficiencias individuales**

������1����ϵ�� ∑  
������������������ ��������������  
Matemáticamente equivalente a la Opción A cuando el número de árbitros es fijo, ya que dividir por una constante no altera el óptimo. No aporta ventaja adicional respecto a la suma.

**Opción C — Eficiencia global agregada**

∑ ������������������  
��������ϵ��

∑ ��������������  
��ϵ��

Es la formulación más natural: mide cuánto ingresa el colectivo por hora total invertida. El problema es que enmascara la desigualdad entre árbitros.

**Opción D — Eficiencia del árbitro más perjudicado (maximin)**

������ ��������ϵ��  
������������������ ��������������

Optimiza exclusivamente para el árbitro en peor situación. Aunque puede linealizarse con una variable auxiliar, puede sacrificar eficiencia global para proteger un único árbitro que, por la configuración de la jornada, no tiene opciones favorables.  
**Dilema de la Equidad**

La equidad en la distribución del trabajo entre árbitros puede incorporarse al modelo de dos formas matemáticamente distintas.

La primera consiste en incluir la varianza del cociente ingresos/tiempo de cada árbitro directamente en la función objetivo, como término negativo ponderado por un parámetro λ, de forma que el algoritmo optimice simultáneamente eficiencia y equidad como una sola función.

La segunda consiste en tratarla como una restricción blanda penalizada, manteniendo la eficiencia como único objetivo formal y añadiendo la varianza como coste adicional que el algoritmo intentará minimizar.

Ambas formulaciones producen comportamientos similares en la práctica, pero difieren en su interpretación conceptual: en el primer caso la equidad forma parte de la definición de solución buena; en el segundo, forma parte de la definición de solución cara.

Sin embargo, es importante señalar que maximizar la eficiencia económica y garantizar una distribución equitativa del trabajo son objetivos que pueden entrar en conflicto: una asignación que concentre los mejores partidos en los árbitros mejor situados geográficamente será eficiente pero desigual, mientras que forzar una distribución completamente equilibrada puede degradar la eficiencia global del colectivo. La resolución de este dilema se tomará en la fase de diseño formal del modelo.

**4\. Datos de Entrada**

**Información de cada árbitro**

• Ubicación inicial (domicilio).

• Nivel o categoría de certificación.

• Disponibilidad horaria.

• Medio de transporte disponible (vehículo propio o transporte público). • Vetos: lista de equipos con los que existe incompatibilidad

**Información de cada partido**

• Ubicación de la sede.

• Horario de inicio y duración.

• Nivel de árbitro requerido.

• Tabla de importes por rol (árbitro principal, segundo árbitro, anotador, jueces de línea).  
**Cálculo de tiempos de desplazamiento**

El tiempo de desplazamiento entre dos ubicaciones se calculará de forma dinámica según el perfil del árbitro:

• Si dispone de vehículo propio: mediante rutas de conducción, usando una API de mapas (Google Maps Distance Matrix API, Matrix Routing, OSRM u OpenRouteService basado en OpenStreetMap) o, en su defecto, medias de tiempos precalculadas.

• Si utiliza transporte público: mediante la misma familia de APIs en modo tránsito, añadiendo los márgenes de espera y transbordo necesarios.

**5\. Restricciones**

**1\. Restricciones Inquebrantables**

Determinan si una asignación es físicamente posible. Si se incumple alguna, la solución se descarta por completo.

**Cobertura Total**

Cada partido programado debe tener asignado al menos un árbitro. Si no es posible cubrir un partido, se introduce una variable binaria de incumplimiento que activa una penalización M en la función objetivo. El valor de M debe ser suficientemente alto para que el modelo solo deje partidos sin cubrir cuando sea estrictamente inevitable. En ese caso, el sistema generará una alerta y ofrecerá al designador la posibilidad de intervenir manualmente o aceptar la recomendación del algoritmo.

**Unicidad de Presencia**

Un árbitro no puede estar en dos ubicaciones al mismo tiempo. Sus asignaciones no pueden solaparse ni un solo minuto.

**Factibilidad de Traslado**

Para que un árbitro pueda arbitrar el Partido A y a continuación el Partido B, el tiempo transcurrido desde que finaliza el primero hasta que comienza el segundo debe ser mayor que el tiempo de viaje más un margen de cortesía fijo de 10 minutos para preparación o retrasos mínimos. El cálculo de la ventana de tiempo entre partidos es dinámico según el medio de transporte del árbitro.

**Categoría Técnica**

El árbitro debe poseer el nivel o certificación necesaria para el partido asignado. Un árbitro de nivel superior puede arbitrar partidos de cualquier categoría inferior. Un árbitro de nivel inferior no puede arbitrar un partido de categoría superior.

**Disponibilidad Personal**  
Se debe respetar la disponibilidad horaria declarada por cada árbitro.

**Incompatibilidad y Conflicto de Intereses (Vetos)**

Se prohíbe la asignación de un árbitro a encuentros donde participen equipos con los que exista un vínculo que comprometa la neutralidad. Esto incluye la pertenencia activa al club (como jugador, técnico o socio) o la existencia de incidentes y conflictos previos documentados. Los vetos se almacenan como atributo de cada árbitro.

**2\. Restricciones de Calidad (Penalizaciones)**

Estas reglas no invalidan la solución, pero incrementan su coste. El algoritmo buscará la combinación con el menor coste total posible.

**Penalización por Desplazamiento**

Cada minuto que el árbitro pasa en tránsito suma al coste total. El algoritmo preferirá mantener al árbitro en la misma sede o desplazarlo a una sede cercana antes que enviarlo al otro extremo del área geográfica.

**Penalización por Tiempos Muertos**

Se penalizan las esperas excesivas entre partidos. Cuando el tiempo de espera entre el Partido A y el Partido B supera un umbral determinado, se añade un castigo proporcional al exceso. La función de penalización es no lineal para desincentivar con mayor fuerza las esperas muy prolongadas.

**3\. Parámetros sin restricción**

• No existe límite máximo de partidos asignables a un árbitro por jornada. • No existe restricción de descanso mínimo entre jornadas.

**6\. Interfaz y Sistema de Alertas**

El sistema dispondrá de una interfaz que permitirá al designador visualizar la asignación calculada, revisar las métricas de eficiencia y equidad, y gestionar las situaciones excepcionales.

Cuando la configuración de la jornada impida cubrir todos los partidos o genere incoherencias logísticas severas, el sistema emitirá una alerta. El designador podrá en ese caso intervenir manualmente o aceptar las sugerencias de intercambio de partidos calculadas por el algoritmo para maximizar la productividad del colectivo.  
**7\. Referencias encontradas**

Vallespí Soro, F. (2019). _The Referee Assignment Problem._ Trabajo Final de Grado, Grau en Matemàtiques, Universitat Politècnica de Catalunya. Disponible en: https\://hdl.handle.net/2117/166628
