const BAUD_RATE = 9600;
const RESET_WAIT_MS = 2200;

const connectButton = document.querySelector("#connectButton");
const statusDot = document.querySelector("#statusDot");
const statusText = document.querySelector("#statusText");
const consoleElement = document.querySelector("#console");
const supportWarning = document.querySelector("#supportWarning");

const controls = {
  left: document.querySelector("#leftPulse"),
  right: document.querySelector("#rightPulse"),
  mouth: document.querySelector("#mouthPulse"),
};

let port = null;
let reader = null;
let keepReading = false;

function appendLog(message, prefix = "") {
  const timestamp = new Date().toLocaleTimeString("pt-BR", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  consoleElement.textContent += `\n[${timestamp}] ${prefix}${message}`;
  consoleElement.scrollTop = consoleElement.scrollHeight;
}

function setConnected(connected) {
  statusDot.classList.toggle("connected", connected);
  statusText.textContent = connected ? "Arduino conectado" : "Desconectado";
  connectButton.textContent = connected ? "Desconectar" : "Conectar Arduino";

  document.querySelectorAll("[data-command], #moveLeft, #moveRight, #moveMouth, #moveArms, #moveAll")
    .forEach((element) => {
      element.disabled = !connected;
    });
}

async function readSerial() {
  const decoder = new TextDecoder();
  let pending = "";

  while (port?.readable && keepReading) {
    reader = port.readable.getReader();
    try {
      while (keepReading) {
        const { value, done } = await reader.read();
        if (done) break;
        pending += decoder.decode(value, { stream: true });
        const lines = pending.split(/\r?\n/);
        pending = lines.pop() ?? "";
        lines.filter(Boolean).forEach((line) => appendLog(line, "Arduino: "));
      }
    } catch (error) {
      if (keepReading) appendLog(error.message, "Erro de leitura: ");
    } finally {
      reader.releaseLock();
      reader = null;
    }
  }

  if (pending.trim()) appendLog(pending.trim(), "Arduino: ");
}

async function connect() {
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: BAUD_RATE });
    keepReading = true;
    setConnected(true);
    appendLog("Conectado. Aguardando reinicialização da placa…");
    readSerial();
    await new Promise((resolve) => setTimeout(resolve, RESET_WAIT_MS));
    await sendCommand("STATUS");
  } catch (error) {
    port = null;
    setConnected(false);
    appendLog(error.message, "Falha ao conectar: ");
  }
}

async function disconnect() {
  keepReading = false;
  try {
    if (reader) await reader.cancel();
    if (port) await port.close();
  } catch (error) {
    appendLog(error.message, "Falha ao desconectar: ");
  } finally {
    reader = null;
    port = null;
    setConnected(false);
    appendLog("Arduino desconectado.");
  }
}

async function sendCommand(command) {
  if (!port?.writable) {
    appendLog("Conecte o Arduino primeiro.", "Aviso: ");
    return;
  }

  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(`${command}\n`));
    appendLog(command, "Você: ");
  } catch (error) {
    appendLog(error.message, "Falha ao enviar: ");
  } finally {
    writer.releaseLock();
  }
}

function bindSlider(input, outputId) {
  const output = document.querySelector(outputId);
  input.addEventListener("input", () => {
    output.value = input.value;
  });
}

connectButton.addEventListener("click", () => {
  if (port) disconnect();
  else connect();
});

document.querySelectorAll("[data-command]").forEach((button) => {
  button.addEventListener("click", () => sendCommand(button.dataset.command));
});

document.querySelector("#moveLeft").addEventListener("click", () => {
  sendCommand(`MOVER_ESQUERDO ${controls.left.value}`);
});

document.querySelector("#moveRight").addEventListener("click", () => {
  sendCommand(`MOVER_DIREITO ${controls.right.value}`);
});

document.querySelector("#moveMouth").addEventListener("click", () => {
  sendCommand(`MOVER B ${controls.mouth.value}`);
});

document.querySelector("#moveArms").addEventListener("click", () => {
  sendCommand(`MOVER_BRACOS ${controls.left.value} ${controls.right.value}`);
});

document.querySelector("#moveAll").addEventListener("click", () => {
  sendCommand(`MOVER_TUDO ${controls.left.value} ${controls.right.value} ${controls.mouth.value}`);
});

document.querySelector("#clearConsole").addEventListener("click", () => {
  consoleElement.textContent = "Console limpo.";
});

bindSlider(controls.left, "#leftValue");
bindSlider(controls.right, "#rightValue");
bindSlider(controls.mouth, "#mouthValue");

if (!("serial" in navigator)) {
  supportWarning.hidden = false;
  connectButton.disabled = true;
}

setConnected(false);
