/**
 * Rocky.js Pebble Watchface Template
 *
 * This template provides a foundation for creating JavaScript-based
 * watchfaces using Rocky.js. Suitable for simpler designs or
 * developers more comfortable with JavaScript.
 *
 * Note: Rocky.js watchfaces may have slightly higher battery usage
 * compared to C watchfaces.
 */

// Import Rocky.js
var rocky = require('rocky');

// ============================================================================
// CONFIGURATION
// ============================================================================

var CONFIG = {
    backgroundColor: 'black',
    foregroundColor: 'white',
    accentColor: 'cyan',

    // Clock settings
    showSeconds: false,
    use24Hour: false,

    // Font sizes
    timeFontSize: 42,
    dateFontSize: 18
};

// ============================================================================
// STATE
// ============================================================================

var state = {
    time: null,
    battery: 100
};

// ============================================================================
// DRAWING FUNCTIONS
// ============================================================================

function drawBackground(ctx, bounds) {
    ctx.fillStyle = CONFIG.backgroundColor;
    ctx.fillRect(0, 0, bounds.w, bounds.h);
}

function drawTime(ctx, bounds) {
    var hours = state.time.getHours();
    var minutes = state.time.getMinutes();

    // Convert to 12-hour format if needed
    if (!CONFIG.use24Hour) {
        hours = hours % 12 || 12;
    }

    // Format time string
    var timeStr = hours.toString().padStart(2, '0') + ':' +
                  minutes.toString().padStart(2, '0');

    // Draw time
    ctx.fillStyle = CONFIG.foregroundColor;
    ctx.textAlign = 'center';
    ctx.font = CONFIG.timeFontSize + 'px bold Gothic';

    var centerX = bounds.w / 2;
    var centerY = bounds.h / 2;

    ctx.fillText(timeStr, centerX, centerY);
}

function drawDate(ctx, bounds) {
    var days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
                  'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

    var day = days[state.time.getDay()];
    var month = months[state.time.getMonth()];
    var date = state.time.getDate();

    var dateStr = day + ', ' + month + ' ' + date;

    ctx.fillStyle = CONFIG.foregroundColor;
    ctx.textAlign = 'center';
    ctx.font = CONFIG.dateFontSize + 'px Gothic';

    var centerX = bounds.w / 2;
    var dateY = bounds.h / 2 + 30;

    ctx.fillText(dateStr, centerX, dateY);
}

function drawBattery(ctx, bounds) {
    var batteryWidth = 20;
    var batteryHeight = 8;
    var x = bounds.w - batteryWidth - 5;
    var y = 5;

    // Battery outline
    ctx.strokeStyle = CONFIG.foregroundColor;
    ctx.strokeRect(x, y, batteryWidth, batteryHeight);

    // Battery fill
    var fillWidth = (state.battery / 100) * batteryWidth;
    ctx.fillStyle = CONFIG.foregroundColor;
    ctx.fillRect(x, y, fillWidth, batteryHeight);

    // Battery tip
    ctx.fillRect(x + batteryWidth, y + 2, 2, batteryHeight - 4);
}

function drawDecorations(ctx, bounds) {
    // Add custom decorations here
    // Example: Simple top and bottom lines

    ctx.strokeStyle = CONFIG.accentColor;
    ctx.lineWidth = 2;

    // Top accent
    ctx.beginPath();
    ctx.moveTo(20, 35);
    ctx.lineTo(bounds.w - 20, 35);
    ctx.stroke();

    // Bottom accent
    ctx.beginPath();
    ctx.moveTo(20, bounds.h - 35);
    ctx.lineTo(bounds.w - 20, bounds.h - 35);
    ctx.stroke();
}

// ============================================================================
// ANALOG CLOCK DRAWING (Optional alternative)
// ============================================================================

function drawAnalogClock(ctx, bounds) {
    var centerX = bounds.w / 2;
    var centerY = bounds.h / 2;
    var radius = Math.min(centerX, centerY) - 10;

    // Clock face
    ctx.strokeStyle = CONFIG.foregroundColor;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, 0, 2 * Math.PI);
    ctx.stroke();

    // Hour markers
    for (var i = 0; i < 12; i++) {
        var angle = (i * Math.PI / 6) - Math.PI / 2;
        var innerRadius = radius - 10;
        var outerRadius = radius - 3;

        var x1 = centerX + Math.cos(angle) * innerRadius;
        var y1 = centerY + Math.sin(angle) * innerRadius;
        var x2 = centerX + Math.cos(angle) * outerRadius;
        var y2 = centerY + Math.sin(angle) * outerRadius;

        ctx.lineWidth = (i % 3 === 0) ? 3 : 1;
        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.lineTo(x2, y2);
        ctx.stroke();
    }

    // Hour hand
    var hours = state.time.getHours() % 12;
    var minutes = state.time.getMinutes();
    var hourAngle = ((hours + minutes / 60) * Math.PI / 6) - Math.PI / 2;
    var hourLength = radius * 0.5;

    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(centerX, centerY);
    ctx.lineTo(
        centerX + Math.cos(hourAngle) * hourLength,
        centerY + Math.sin(hourAngle) * hourLength
    );
    ctx.stroke();

    // Minute hand
    var minuteAngle = (minutes * Math.PI / 30) - Math.PI / 2;
    var minuteLength = radius * 0.75;

    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(centerX, centerY);
    ctx.lineTo(
        centerX + Math.cos(minuteAngle) * minuteLength,
        centerY + Math.sin(minuteAngle) * minuteLength
    );
    ctx.stroke();

    // Center dot
    ctx.fillStyle = CONFIG.foregroundColor;
    ctx.beginPath();
    ctx.arc(centerX, centerY, 4, 0, 2 * Math.PI);
    ctx.fill();
}

// ============================================================================
// MAIN DRAW FUNCTION
// ============================================================================

function draw(ctx, bounds) {
    drawBackground(ctx, bounds);
    drawDecorations(ctx, bounds);
    drawTime(ctx, bounds);
    drawDate(ctx, bounds);
    drawBattery(ctx, bounds);

    // Uncomment to use analog clock instead:
    // drawAnalogClock(ctx, bounds);
}

// ============================================================================
// EVENT HANDLERS
// ============================================================================

rocky.on('draw', function(event) {
    var ctx = event.context;
    var bounds = {
        w: ctx.canvas.unobstructedWidth,
        h: ctx.canvas.unobstructedHeight
    };

    draw(ctx, bounds);
});

rocky.on('minutechange', function(event) {
    state.time = event.date;
    rocky.requestDraw();
});

// For second hand (if enabled)
if (CONFIG.showSeconds) {
    rocky.on('secondchange', function(event) {
        state.time = event.date;
        rocky.requestDraw();
    });
}

// ============================================================================
// INITIALIZATION
// ============================================================================

// Set initial time
state.time = new Date();

// Request initial draw
rocky.requestDraw();
