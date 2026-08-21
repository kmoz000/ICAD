(function (global) {
  "use strict";

  const radians = (degrees) => degrees * Math.PI / 180;
  const lerp = (a, b, amount) => a + (b - a) * amount;

  function eased(mode, amount) {
    if (mode === "STEP") return 0;
    if (mode === "EASE_IN") return amount * amount;
    if (mode === "EASE_OUT") return 1 - (1 - amount) * (1 - amount);
    if (mode === "EASE_IN_OUT") return amount < 0.5 ? 2 * amount * amount : 1 - Math.pow(-2 * amount + 2, 2) / 2;
    return amount;
  }

  function interpolate(track, time) {
    const frames = track.keyframes;
    if (!frames.length) return { position: [0, 0, 0], rotation: [0, 0, 0] };
    if (time <= frames[0].time) return frames[0];
    if (time >= frames[frames.length - 1].time) return frames[frames.length - 1];
    let right = 1;
    while (frames[right].time < time) right += 1;
    const a = frames[right - 1];
    const b = frames[right];
    const amount = eased(track.easing, (time - a.time) / (b.time - a.time));
    if (track.targetKind === "VISIBILITY") return { visible: amount < 1 ? a.visible : b.visible };
    if (track.targetKind === "JOINT") {
      return { value: lerp(a.value, b.value, amount), unit: a.unit };
    }
    return {
      position: a.position.map((value, index) => lerp(value, b.position[index], amount)),
      rotation: a.rotation.map((value, index) => lerp(value, b.rotation[index], amount))
    };
  }

  function rotate(point, rotation) {
    let [x, y, z] = point;
    const [rx, ry, rz] = rotation.map(radians);
    [y, z] = [y * Math.cos(rx) - z * Math.sin(rx), y * Math.sin(rx) + z * Math.cos(rx)];
    [x, z] = [x * Math.cos(ry) + z * Math.sin(ry), -x * Math.sin(ry) + z * Math.cos(ry)];
    [x, y] = [x * Math.cos(rz) - y * Math.sin(rz), x * Math.sin(rz) + y * Math.cos(rz)];
    return [x, y, z];
  }

  function rotateAroundAxis(point, pivot, axis, degrees) {
    const angle = radians(degrees);
    const cosine = Math.cos(angle);
    const sine = Math.sin(angle);
    const relative = point.map((value, index) => value - pivot[index]);
    const cross = [
      axis[1] * relative[2] - axis[2] * relative[1],
      axis[2] * relative[0] - axis[0] * relative[2],
      axis[0] * relative[1] - axis[1] * relative[0]
    ];
    const projection = axis.reduce((sum, value, index) => sum + value * relative[index], 0);
    return relative.map((value, index) => pivot[index] + value * cosine + cross[index] * sine +
      axis[index] * projection * (1 - cosine));
  }

  function animatedOccurrence(point, occurrence, jointsByChild, jointValues) {
    const chain = [];
    let child = occurrence;
    while (jointsByChild.has(child)) {
      const joint = jointsByChild.get(child);
      chain.push(joint);
      if (joint.parent === "WORLD") break;
      child = joint.parent;
    }
    chain.reverse();
    const operations = [];
    let animated = point;
    for (const joint of chain) {
      let pivot = joint.pivotMm.slice();
      let axis = joint.axisUnit.slice();
      for (const operation of operations) {
        if (operation.type === "REVOLUTE") {
          pivot = rotateAroundAxis(pivot, operation.pivot, operation.axis, operation.delta);
          axis = rotateAroundAxis(axis, [0, 0, 0], operation.axis, operation.delta);
        } else if (operation.type === "PRISMATIC") {
          pivot = pivot.map((value, index) => value + operation.axis[index] * operation.delta);
        }
      }
      const delta = (jointValues.has(joint.name) ? jointValues.get(joint.name) : joint.value) - joint.value;
      if (joint.type === "REVOLUTE") animated = rotateAroundAxis(animated, pivot, axis, delta);
      if (joint.type === "PRISMATIC") animated = animated.map((value, index) => value + axis[index] * delta);
      operations.push({ type: joint.type, pivot, axis, delta });
    }
    return animated;
  }

  function mountWebGL(root, model, gl) {
    const vertexSource = `#version 300 es
      in vec3 position; in vec4 color; out vec4 vertexColor;
      void main(){gl_Position=vec4(position,1.0);vertexColor=color;}`;
    const fragmentSource = `#version 300 es
      precision highp float; in vec4 vertexColor; out vec4 outputColor;
      void main(){outputColor=vertexColor;}`;
    function shader(type, source) {
      const value = gl.createShader(type);
      gl.shaderSource(value, source);
      gl.compileShader(value);
      if (!gl.getShaderParameter(value, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(value));
      return value;
    }
    const program = gl.createProgram();
    gl.attachShader(program, shader(gl.VERTEX_SHADER, vertexSource));
    gl.attachShader(program, shader(gl.FRAGMENT_SHADER, fragmentSource));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
    const positionLocation = gl.getAttribLocation(program, "position");
    const colorLocation = gl.getAttribLocation(program, "color");
    const buffer = gl.createBuffer();
    const materials = new Map(model.materials.map(material => [material.name, material]));
    const jointsByChild = new Map(model.joints.map(joint => [joint.child, joint]));
    const selected = [];
    let sceneIndex = 0;
    let playing = true;
    let started = performance.now();
    let viewRotation = [58, 0, -28];
    let zoom = 1;
    let explode = 0;
    let section = 1;
    let drag;

    const controls = document.createElement("div");
    controls.className = "icad-viewer-controls";
    controls.setAttribute("role", "toolbar");
    controls.setAttribute("aria-label", "ICAD viewer controls");
    const play = document.createElement("button");
    play.textContent = "Pause";
    play.setAttribute("aria-label", "Play or pause scene animation");
    play.onclick = () => { playing = !playing; play.textContent = playing ? "Pause" : "Play"; started = performance.now(); if (playing) requestAnimationFrame(draw); };
    controls.appendChild(play);
    const explodeInput = document.createElement("input");
    explodeInput.type = "range"; explodeInput.min = "0"; explodeInput.max = "1"; explodeInput.step = "0.05";
    explodeInput.setAttribute("aria-label", "Assembly explode amount");
    explodeInput.oninput = () => { explode = Number(explodeInput.value); draw(performance.now()); };
    controls.appendChild(explodeInput);
    const sectionInput = document.createElement("input");
    sectionInput.type = "range"; sectionInput.min = "-1"; sectionInput.max = "1"; sectionInput.step = "0.02"; sectionInput.value = "1";
    sectionInput.setAttribute("aria-label", "Section plane position");
    sectionInput.oninput = () => { section = Number(sectionInput.value); draw(performance.now()); };
    controls.appendChild(sectionInput);
    if (model.scenes.length > 1) {
      const picker = document.createElement("select");
      picker.setAttribute("aria-label", "Animation scene");
      model.scenes.forEach((sceneValue, index) => {
        const option = document.createElement("option"); option.value = String(index); option.textContent = sceneValue.name; picker.appendChild(option);
      });
      picker.onchange = () => { sceneIndex = Number(picker.value); started = performance.now(); };
      controls.appendChild(picker);
    }
    root.parentElement.insertBefore(controls, root);

    const tree = document.createElement("aside");
    tree.className = "icad-semantic-tree";
    tree.setAttribute("aria-label", "Design component tree");
    const status = document.createElement("output");
    status.className = "icad-measurement";
    tree.appendChild(status);
    for (const body of [...new Set(model.parts.map(part => part.body))]) {
      const item = document.createElement("button");
      item.textContent = body;
      item.onclick = () => {
        const found = selected.indexOf(body);
        if (found >= 0) selected.splice(found, 1); else { selected.push(body); if (selected.length > 2) selected.shift(); }
        item.setAttribute("aria-pressed", String(selected.includes(body)));
        draw(performance.now());
      };
      item.setAttribute("aria-pressed", "false");
      tree.appendChild(item);
    }
    root.parentElement.appendChild(tree);

    root.tabIndex = 0;
    root.setAttribute("aria-label", "Interactive ICAD WebGL design viewport");
    root.addEventListener("pointerdown", event => { drag = [event.clientX, event.clientY]; root.setPointerCapture(event.pointerId); });
    root.addEventListener("pointermove", event => { if (!drag) return; viewRotation[2] += (event.clientX - drag[0]) * 0.35; viewRotation[0] += (event.clientY - drag[1]) * 0.35; drag = [event.clientX, event.clientY]; draw(performance.now()); });
    root.addEventListener("pointerup", () => { drag = undefined; });
    root.addEventListener("wheel", event => { event.preventDefault(); zoom = Math.max(0.2, Math.min(6, zoom * Math.exp(-event.deltaY * 0.001))); draw(performance.now()); }, { passive: false });

    const all = model.parts.flatMap(part => part.vertices);
    const minimum = [0, 1, 2].map(axis => Math.min(...all.map(point => point[axis])));
    const maximum = [0, 1, 2].map(axis => Math.max(...all.map(point => point[axis])));
    const center = minimum.map((value, axis) => (value + maximum[axis]) / 2);
    const extent = Math.max(...maximum.map((value, axis) => value - minimum[axis]), 1);

    function draw(now) {
      const ratio = global.devicePixelRatio || 1;
      const width = root.clientWidth || 960; const height = root.clientHeight || 640;
      if (root.width !== width * ratio || root.height !== height * ratio) { root.width = width * ratio; root.height = height * ratio; }
      gl.viewport(0, 0, root.width, root.height);
      const scene = model.scenes[sceneIndex];
      const duration = scene ? scene.duration : 1;
      const elapsed = playing ? (now - started) / 1000 : 0;
      const time = scene ? Math.min(duration, elapsed % Math.max(duration, 0.001)) : 0;
      const transforms = new Map(); const jointValues = new Map(); const visibility = new Map();
      let cameraRotation = viewRotation;
      if (scene) for (const track of scene.tracks) {
        const value = interpolate(track, time);
        if (track.targetKind === "BODY") transforms.set(track.target, value);
        if (track.targetKind === "CAMERA") cameraRotation = value.rotation.map((entry, axis) => entry + viewRotation[axis]);
        if (track.targetKind === "JOINT") jointValues.set(track.target, value.value);
        if (track.targetKind === "VISIBILITY") visibility.set(track.target, value.visible);
      }
      const vertices = [];
      const centers = new Map();
      for (const part of model.parts) {
        if (visibility.get(part.body) === false) continue;
        const transform = transforms.get(part.body) || { position: [0, 0, 0], rotation: [0, 0, 0] };
        const partCenter = [0, 1, 2].map(axis => part.vertices.reduce((sum, point) => sum + point[axis], 0) / part.vertices.length);
        centers.set(part.body, partCenter);
        const direction = partCenter.map((value, axis) => (value - center[axis]) / extent);
        const color = selected.includes(part.body) ? [1, 0.72, 0.08, 1] : (materials.get(part.material)?.baseColor || [0.58, 0.64, 0.7, 1]);
        for (const face of part.triangles) {
          const sourcePoints = face.map(index => part.vertices[index]);
          if (sourcePoints.reduce((sum, point) => sum + point[2], 0) / 3 > center[2] + section * extent) continue;
          for (const sourcePoint of sourcePoints) {
            let point = animatedOccurrence(sourcePoint.slice(), part.body, jointsByChild, jointValues);
            point = point.map((value, axis) => value + direction[axis] * explode * extent * 0.4);
            point = rotate(point.map((value, axis) => value - center[axis]), transform.rotation);
            point = point.map((value, axis) => value + transform.position[axis]);
            point = rotate(point, cameraRotation);
            const viewportScale = 1.52 * Math.min(width, height);
            vertices.push(point[0] / extent * zoom * viewportScale / width,
              point[1] / extent * zoom * viewportScale / height,
              Math.max(-0.99, Math.min(0.99, point[2] / extent)), ...color);
          }
        }
      }
      if (selected.length === 2 && centers.has(selected[0]) && centers.has(selected[1])) {
        const first = centers.get(selected[0]); const second = centers.get(selected[1]);
        const distance = Math.hypot(...first.map((value, axis) => value - second[axis]));
        status.textContent = `${selected[0]} ↔ ${selected[1]} centroid distance: ${distance.toFixed(3)} mm`;
      } else status.textContent = selected.length ? `Selected: ${selected.join(", ")}` : "Select up to two components to measure";
      const background = scene?.background === "NIGHT" ? [0.027, 0.063, 0.114, 1] : [0.91, 0.93, 0.95, 1];
      gl.clearColor(...background); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT); gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE);
      gl.useProgram(program); gl.bindBuffer(gl.ARRAY_BUFFER, buffer); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(positionLocation); gl.vertexAttribPointer(positionLocation, 3, gl.FLOAT, false, 28, 0);
      gl.enableVertexAttribArray(colorLocation); gl.vertexAttribPointer(colorLocation, 4, gl.FLOAT, false, 28, 12);
      gl.drawArrays(gl.TRIANGLES, 0, vertices.length / 7);
      if (playing) requestAnimationFrame(draw);
    }
    requestAnimationFrame(draw);
    return { backend: "webgl2", play: () => { playing = true; started = performance.now(); requestAnimationFrame(draw); }, pause: () => { playing = false; }, select: body => { selected.splice(0, selected.length, body); draw(performance.now()); } };
  }

  function mountCanvas(root, model) {
    if (!root || !model) throw new Error("ICADViewer.mount requires a canvas and compiled model");
    const context = root.getContext("2d");
    const materials = new Map(model.materials.map((material) => [material.name, material]));
    const jointsByChild = new Map(model.joints.map((joint) => [joint.child, joint]));
    const patterns = new Map();
    let sceneIndex = 0;
    let playing = true;
    let started = performance.now();
    let viewRotation = [58, 0, -28];
    let zoom = 1;
    const emittedEvents = new Set();

    for (const material of model.materials) {
      const image = new Image();
      image.onload = () => {
        patterns.set(material.name, context.createPattern(image, "repeat"));
        draw(performance.now());
      };
      image.src = material.texture.dataUri;
    }

    const controls = document.createElement("div");
    controls.className = "icad-viewer-controls";
    const play = document.createElement("button");
    play.textContent = "Pause";
    play.onclick = () => {
      playing = !playing;
      play.textContent = playing ? "Pause" : "Play";
      started = performance.now();
      if (playing) requestAnimationFrame(draw);
    };
    controls.appendChild(play);
    if (model.scenes.length > 1) {
      const picker = document.createElement("select");
      model.scenes.forEach((scene, index) => {
        const option = document.createElement("option");
        option.value = String(index);
        option.textContent = scene.name;
        picker.appendChild(option);
      });
      picker.onchange = () => {
        sceneIndex = Number(picker.value);
        started = performance.now();
        emittedEvents.clear();
      };
      controls.appendChild(picker);
    }
    root.parentElement.insertBefore(controls, root);

    let drag;
    root.addEventListener("pointerdown", (event) => { drag = [event.clientX, event.clientY]; root.setPointerCapture(event.pointerId); });
    root.addEventListener("pointermove", (event) => {
      if (!drag) return;
      viewRotation[2] += (event.clientX - drag[0]) * 0.35;
      viewRotation[0] += (event.clientY - drag[1]) * 0.35;
      drag = [event.clientX, event.clientY];
    });
    root.addEventListener("pointerup", () => { drag = undefined; });
    root.addEventListener("wheel", (event) => {
      event.preventDefault();
      zoom = Math.max(0.2, Math.min(6, zoom * Math.exp(-event.deltaY * 0.001)));
    }, { passive: false });

    function draw(now) {
      const ratio = global.devicePixelRatio || 1;
      const width = root.clientWidth || 960;
      const height = root.clientHeight || 640;
      if (root.width !== width * ratio || root.height !== height * ratio) {
        root.width = width * ratio;
        root.height = height * ratio;
      }
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
      const scene = model.scenes[sceneIndex];
      const duration = scene ? scene.duration : 1;
      const elapsed = playing ? (now - started) / 1000 : 0;
      const loopCount = scene ? scene.loopCount || 1 : 1;
      const finished = elapsed >= duration * loopCount;
      const time = finished ? duration : elapsed % duration;
      const cycle = finished ? loopCount - 1 : Math.floor(elapsed / duration);
      if (finished) {
        playing = false;
        play.textContent = "Play";
      }
      const background = scene && scene.background === "NIGHT" ? "#07101d" : "#e9edf2";
      context.fillStyle = background;
      context.fillRect(0, 0, width, height);

      const transforms = new Map();
      const jointValues = new Map();
      const visibility = new Map();
      let cameraRotation = viewRotation;
      if (scene) {
        for (const track of scene.tracks) {
          const value = interpolate(track, time);
          if (track.targetKind === "BODY") transforms.set(track.target, value);
          if (track.targetKind === "CAMERA") cameraRotation = value.rotation.map((v, i) => v + viewRotation[i]);
          if (track.targetKind === "JOINT") jointValues.set(track.target, value.value);
          if (track.targetKind === "VISIBILITY") visibility.set(track.target, value.visible);
        }
        for (const event of scene.events || []) {
          const eventKey = `${cycle}:${event.name}`;
          if (event.time <= time && !emittedEvents.has(eventKey)) {
            emittedEvents.add(eventKey);
            root.dispatchEvent(new CustomEvent("icad-scene-event", { detail: { scene: scene.name, ...event } }));
          }
        }
      }

      function animateOccurrence(point, occurrence) {
        const chain = [];
        let child = occurrence;
        while (jointsByChild.has(child)) {
          const joint = jointsByChild.get(child);
          chain.push(joint);
          if (joint.parent === "WORLD") break;
          child = joint.parent;
        }
        chain.reverse();
        const operations = [];
        let animated = point;
        for (const joint of chain) {
          let pivot = joint.pivotMm.slice();
          let axis = joint.axisUnit.slice();
          for (const operation of operations) {
            if (operation.type === "REVOLUTE") {
              pivot = rotateAroundAxis(pivot, operation.pivot, operation.axis, operation.delta);
              axis = rotateAroundAxis(axis, [0, 0, 0], operation.axis, operation.delta);
            } else if (operation.type === "PRISMATIC") {
              pivot = pivot.map((value, index) => value + operation.axis[index] * operation.delta);
            }
          }
          const delta = (jointValues.has(joint.name) ? jointValues.get(joint.name) : joint.value) - joint.value;
          if (joint.type === "REVOLUTE") {
            animated = rotateAroundAxis(animated, pivot, axis, delta);
          } else if (joint.type === "PRISMATIC") {
            animated = animated.map((value, index) => value + axis[index] * delta);
          }
          operations.push({ type: joint.type, pivot, axis, delta });
        }
        return animated;
      }

      const all = model.parts.flatMap((part) => part.vertices);
      const minimum = [Math.min(...all.map((p) => p[0])), Math.min(...all.map((p) => p[1])), Math.min(...all.map((p) => p[2]))];
      const maximum = [Math.max(...all.map((p) => p[0])), Math.max(...all.map((p) => p[1])), Math.max(...all.map((p) => p[2]))];
      const center = minimum.map((value, index) => (value + maximum[index]) / 2);
      const extent = Math.max(...maximum.map((value, index) => value - minimum[index]));
      const scale = 0.76 * Math.min(width, height) / Math.max(extent, 1) * zoom;
      const triangles = [];

      for (const part of model.parts) {
        if (visibility.get(part.body) === false) continue;
        const transform = transforms.get(part.body) || { position: [0, 0, 0], rotation: [0, 0, 0] };
        const projected = part.vertices.map((vertex) => {
          let point = animateOccurrence(vertex.slice(), part.body);
          point = rotate(point.map((value, index) => value - center[index]), transform.rotation);
          point = point.map((value, index) => value + transform.position[index]);
          point = rotate(point, cameraRotation);
          return [width / 2 + point[0] * scale, height / 2 - point[1] * scale, point[2]];
        });
        for (const face of part.triangles) {
          const points = face.map((index) => projected[index]);
          triangles.push({ points, depth: points.reduce((sum, point) => sum + point[2], 0) / 3, material: part.material });
        }
      }
      triangles.sort((a, b) => a.depth - b.depth);
      for (const triangle of triangles) {
        context.beginPath();
        context.moveTo(triangle.points[0][0], triangle.points[0][1]);
        context.lineTo(triangle.points[1][0], triangle.points[1][1]);
        context.lineTo(triangle.points[2][0], triangle.points[2][1]);
        context.closePath();
        const material = materials.get(triangle.material);
        const color = material ? material.baseColor.slice(0, 3).map((v) => Math.round(v * 255)) : [150, 160, 170];
        context.fillStyle = patterns.get(triangle.material) || `rgb(${color.join(",")})`;
        context.globalAlpha = material ? material.baseColor[3] : 1;
        context.fill();
        context.globalAlpha = 1;
        context.strokeStyle = "rgba(10,20,30,.16)";
        context.lineWidth = 0.5;
        context.stroke();
      }
      context.fillStyle = background === "#07101d" ? "#dbeafe" : "#263241";
      context.font = "13px system-ui";
      context.fillText(`${model.project} · ${scene ? scene.name : "static"} · ${time.toFixed(2)}s`, 14, height - 16);
      if (playing) requestAnimationFrame(draw);
    }
    requestAnimationFrame(draw);
    return {
      play: () => { playing = true; started = performance.now(); emittedEvents.clear(); requestAnimationFrame(draw); },
      pause: () => { playing = false; }
    };
  }

  function mount(root, model) {
    if (!root || !model) throw new Error("ICADViewer.mount requires a canvas and compiled model");
    const gl = root.getContext("webgl2", { antialias: true, alpha: false, depth: true });
    if (gl) return mountWebGL(root, model, gl);
    return mountCanvas(root, model);
  }

  global.ICADViewer = Object.freeze({ mount });
})(window);
