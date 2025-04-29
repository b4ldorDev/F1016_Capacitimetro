# Configuración para desplegar el proyecto en Netlify

## Preparación del código para ESP32-CAM

Para que nuestro ESP32-CAM pueda comunicarse con una página web alojada en Netlify, debemos resolver el problema del CORS (Cross-Origin Resource Sharing). Esto se puede hacer de dos maneras:

1. **Solución lado ESP32**: Modificar el código del ESP32-CAM para permitir solicitudes de dominios externos.
2. **Solución lado web**: Usar un proxy para las solicitudes API.

Vamos a implementar ambas soluciones:

### 1. Modificación del código ESP32-CAM para permitir CORS

En el archivo principal de Arduino, agrega los siguientes encabezados CORS en la sección donde configuras las rutas del servidor:

```cpp
// Configurar CORS
DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
```

### 2. Estructura para el despliegue en Netlify

Para el despliegue web en Netlify, necesitamos crear una estructura de archivos separada. La web en Netlify se comunicará con el ESP32-CAM a través de su dirección IP.

#### Estructura de directorios

```
netlify-capacimeter/
├── index.html
├── style.css
├── script.js
├── netlify.toml
└── functions/
    └── proxy.js
```

## Archivos para despliegue en Netlify

### 1. netlify.toml

```toml
[build]
  publish = "."
  functions = "functions"

[[redirects]]
  from = "/api/*"
  to = "/.netlify/functions/proxy/:splat"
  status = 200
```

### 2. functions/proxy.js (para resolver CORS)

```javascript
const axios = require('axios');

exports.handler = async function(event, context) {
  // La IP del ESP32-CAM debe ser configurada por el usuario
  const espIp = event.queryStringParameters.ip || "192.168.1.100";
  const path = event.path.replace('/api/', '');
  
  try {
    const response = await axios.get(`http://${espIp}/${path}`);
    
    return {
      statusCode: 200,
      body: JSON.stringify(response.data),
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      }
    };
  } catch (error) {
    return {
      statusCode: 500,
      body: JSON.stringify({ error: 'Error al conectar con el ESP32-CAM' }),
      headers: {
        'Content-Type': 'application/json',
        'Access-Control-Allow-Origin': '*'
      }
    };
  }
};
```

### 3. script.js (para Netlify)

```javascript
// Configuración
const useLocalESP = false; // Cambiar a false para usar la función proxy de Netlify
let espIp = ""; // IP del ESP32-CAM, se configurará a través de la interfaz

// Elementos DOM
const ipInput = document.getElementById('esp-ip');
const connectBtn = document.getElementById('connect-btn');
const statusIndicator = document.getElementById('connection-status');

// Configuración de Chart.js
const ctx = document.getElementById('capacitanceChart').getContext('2d');
const capacitanceChart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: Array(100).fill(''),
        datasets: [{
            label: 'Capacitancia',
            data: Array(100).fill(0),
            borderColor: '#3498db',
            backgroundColor: 'rgba(52, 152, 219, 0.2)',
            borderWidth: 2,
            fill: true,
            tension: 0,
            pointRadius: 0
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
            y: {
                beginAtZero: true,
                title: {
                    display: true,
                    text: 'Capacitancia'
                }
            },
            x: {
                title: {
                    display: true,
                    text: 'Tiempo'
                }
            }
        },
        animation: {
            duration: 0
        }
    }
});

// Variables para almacenar datos
let historyData = [];
let chartUnit = "pF";
let eventSource = null;

// Función para conectar con el ESP32-CAM
function connectToESP() {
    espIp = ipInput.value;
    
    if (!espIp) {
        alert("Por favor, introduce la dirección IP del ESP32-CAM");
        return;
    }
    
    // Guardar IP en localStorage
    localStorage.setItem('espIp', espIp);
    
    // Actualizar indicador de estado
    statusIndicator.textContent = "Conectando...";
    statusIndicator.style.color = "orange";
    
    // Cerrar cualquier conexión existente
    if (eventSource) {
        eventSource.close();
    }
    
    // Intentar conectar con el ESP32-CAM
    fetch(getApiUrl('/data'))
        .then(response => {
            if (!response.ok) {
                throw new Error('No se pudo conectar al ESP32-CAM');
            }
            return response.json();
        })
        .then(data => {
            // Conexión exitosa
            statusIndicator.textContent = "Conectado";
            statusIndicator.style.color = "green";
            
            // Inicializar SSE
            setupEventSource();
            
            // Actualizar datos iniciales
            updateCurrentValue(data.capacitance.toFixed(2), data.unit, data.scale);
            chartUnit = data.unit;
            updateChart(data.history, data.unit);
        })
        .catch(error => {
            statusIndicator.textContent = "Error de conexión";
            statusIndicator.style.color = "red";
            console.error('Error:', error);
        });
}

// Función para configurar Server-Sent Events
function setupEventSource() {
    if (useLocalESP) {
        // Modo local: conectar directamente al ESP32-CAM
        eventSource = new EventSource(`http://${espIp}/events`);
    } else {
        // Modo Netlify: usar función proxy
        // Nota: Esto no funcionará con SSE, es solo para demostración
        // En la práctica, usaríamos polling periódico para datos
        pollData();
        return;
    }
    
    eventSource.addEventListener('capacitance_data', function(e) {
        const data = JSON.parse(e.data);
        processData(data);
    });
    
    eventSource.onerror = function() {
        statusIndicator.textContent = "Conexión perdida";
        statusIndicator.style.color = "red";
        eventSource.close();
    };
}

// Función para hacer polling de datos (alternativa a SSE)
function pollData() {
    setInterval(() => {
        fetch(getApiUrl('/data'))
            .then(response => response.json())
            .then(data => {
                processData(data);
            })
            .catch(error => {
                console.error('Error al obtener datos:', error);
                statusIndicator.textContent = "Error de conexión";
                statusIndicator.style.color = "red";
            });
    }, 1000); // Actualizar cada segundo
}

// Función para procesar datos recibidos
function processData(data) {
    // Actualizar valor actual
    updateCurrentValue(data.capacitance.toFixed(2), data.unit, data.scale);
    
    // Actualizar gráfico
    chartUnit = data.unit;
    updateChart(data.history, data.unit);
    
    // Actualizar tabla de historial
    updateHistoryTable(data);
}

// Función para obtener la URL correcta según el modo
function getApiUrl(path) {
    if (useLocalESP) {
        return `http://${espIp}${path}`;
    } else {
        return `/api${path}?ip=${espIp}`;
    }
}

// Funciones para actualizar la UI (las mismas que en index.html)
function updateCurrentValue(value, unit, scale) {
    document.getElementById('capacitance-value').textContent = `${value} ${unit}`;
    document.getElementById('scale-mode').textContent = scale;
}

function updateChart(historyData, unit) {
    capacitanceChart.options.scales.y.title.text = `Capacitancia (${unit})`;
    capacitanceChart.data.datasets[0].data = historyData;
    capacitanceChart.update();
}

function updateHistoryTable(data) {
    const tbody = document.getElementById('history-data');
    
    const row = document.createElement('tr');
    
    const timeCell = document.createElement('td');
    const now = new Date();
    timeCell.textContent = now.toLocaleTimeString();
    row.appendChild(timeCell);
    
    const valueCell = document.createElement('td');
    valueCell.textContent = data.capacitance.toFixed(2);
    row.appendChild(valueCell);
    
    const unitCell = document.createElement('td');
    unitCell.textContent = data.unit;
    row.appendChild(unitCell);
    
    tbody.insertBefore(row, tbody.firstChild);
    
    if (tbody.children.length > 10) {
        tbody.removeChild(tbody.lastChild);
    }
}

// Evento de carga de la página
document.addEventListener('DOMContentLoaded', () => {
    // Cargar IP guardada si existe
    const savedIp = localStorage.getItem('espIp');
    if (savedIp) {
        ipInput.value = savedIp;
    }
    
    // Configurar evento de botón de conexión
    connectBtn.addEventListener('click', connectToESP);
});
```

### 4. index.html (para Netlify)

```html
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width
