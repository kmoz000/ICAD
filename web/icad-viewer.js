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

  function modelBounds(model) {
    const minimum = [Infinity, Infinity, Infinity];
    const maximum = [-Infinity, -Infinity, -Infinity];
    for (const part of model.parts) {
      for (const point of part.vertices) {
        for (let axis = 0; axis < 3; axis += 1) {
          minimum[axis] = Math.min(minimum[axis], point[axis]);
          maximum[axis] = Math.max(maximum[axis], point[axis]);
        }
      }
    }
    const center = minimum.map((value, axis) => (value + maximum[axis]) / 2);
    const extent = Math.max(1, ...maximum.map((value, axis) => value - minimum[axis]));
    return { center, extent };
  }

  function pointInTriangle(x, y, triangle) {
    const [a, b, c] = triangle;
    const area = (p, q, r) => (p[0] - r[0]) * (q[1] - r[1]) -
      (q[0] - r[0]) * (p[1] - r[1]);
    const first = area([x, y], a, b);
    const second = area([x, y], b, c);
    const third = area([x, y], c, a);
    return (first >= 0 && second >= 0 && third >= 0) ||
      (first <= 0 && second <= 0 && third <= 0);
  }

  function addViewCube(parent, setView) {
    const cube = document.createElement("nav");
    cube.className = "icad-view-cube";
    cube.setAttribute("aria-label", "Orthographic view cube");
    const views = [
      ["Top", "T", [0, 0, 0]], ["Left", "L", [90, 0, -90]],
      ["Front", "F", [90, 0, 0]], ["Right", "R", [90, 0, 90]],
      ["Back", "B", [90, 0, 180]], ["Perspective", "3D", [58, 0, -28]],
      ["Bottom", "D", [180, 0, 0]]
    ];
    for (const [name, label, rotation] of views) {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = label;
      button.title = `${name} view`;
      button.setAttribute("aria-label", `${name} orthographic view`);
      button.onclick = () => setView(rotation.slice());
      cube.appendChild(button);
    }
    parent.appendChild(cube);
  }

  function addDropMenu(parent, label, className) {
    const menu = document.createElement("details");
    menu.className = `icad-drop-menu ${className}`;
    const summary = document.createElement("summary");
    summary.textContent = label;
    menu.appendChild(summary);
    const content = document.createElement("div");
    content.className = "icad-drop-content";
    menu.appendChild(content);
    parent.appendChild(menu);
    return { menu, content };
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
    let playing = false;
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
    play.textContent = "Play";
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
    root.parentElement.insertBefore(controls, root);

    const componentDrop = addDropMenu(root.parentElement, "Components", "icad-component-menu");
    const status = document.createElement("output");
    status.className = "icad-measurement";
    componentDrop.content.appendChild(status);
    const componentButtons = new Map();
    const toggleSelection = body => {
      const found = selected.indexOf(body);
      if (found >= 0) selected.splice(found, 1);
      else { selected.push(body); if (selected.length > 2) selected.shift(); }
      for (const [name, button] of componentButtons)
        button.setAttribute("aria-pressed", String(selected.includes(name)));
      draw(performance.now());
    };
    for (const body of [...new Set(model.parts.map(part => part.body))]) {
      const item = document.createElement("button");
      item.textContent = body;
      item.onclick = () => toggleSelection(body);
      item.setAttribute("aria-pressed", "false");
      componentButtons.set(body, item);
      componentDrop.content.appendChild(item);
    }
    const sceneDrop = addDropMenu(root.parentElement, "Scenes", "icad-scene-menu");
    if (model.scenes.length === 0) {
      const empty = document.createElement("span"); empty.textContent = "Static model";
      sceneDrop.content.appendChild(empty);
    }
    model.scenes.forEach((sceneValue, index) => {
      const item = document.createElement("button");
      item.textContent = `▶ ${sceneValue.name}`;
      item.onclick = () => { sceneIndex = index; playing = true; started = performance.now(); play.textContent = "Pause"; requestAnimationFrame(draw); sceneDrop.menu.open = false; };
      sceneDrop.content.appendChild(item);
    });
    addViewCube(root.parentElement, rotation => { viewRotation = rotation; draw(performance.now()); });

    root.tabIndex = 0;
    root.setAttribute("aria-label", "Interactive ICAD WebGL design viewport");
    let pointerStart;
    let pickTriangles = [];
    root.addEventListener("pointerdown", event => { drag = [event.clientX, event.clientY]; pointerStart = drag.slice(); root.setPointerCapture(event.pointerId); });
    root.addEventListener("pointermove", event => { if (!drag) return; viewRotation[2] += (event.clientX - drag[0]) * 0.35; viewRotation[0] += (event.clientY - drag[1]) * 0.35; drag = [event.clientX, event.clientY]; draw(performance.now()); });
    root.addEventListener("pointerup", event => {
      const clicked = pointerStart && Math.hypot(event.clientX - pointerStart[0], event.clientY - pointerStart[1]) < 4;
      drag = undefined;
      if (!clicked) return;
      const bounds = root.getBoundingClientRect();
      const x = event.clientX - bounds.left; const y = event.clientY - bounds.top;
      const hit = pickTriangles.filter(candidate => pointInTriangle(x, y, candidate.points))
        .sort((first, second) => first.depth - second.depth)[0];
      if (hit) toggleSelection(hit.body);
    });
    root.addEventListener("wheel", event => { event.preventDefault(); zoom = Math.max(0.2, Math.min(6, zoom * Math.exp(-event.deltaY * 0.001))); draw(performance.now()); }, { passive: false });

    const { center, extent } = modelBounds(model);

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
      const nextPickTriangles = [];
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
          const transformed = sourcePoints.map(sourcePoint => {
            let point = animatedOccurrence(sourcePoint.slice(), part.body, jointsByChild, jointValues);
            point = rotate(point.map((value, axis) => value - partCenter[axis]), transform.rotation)
              .map((value, axis) => value + partCenter[axis] + transform.position[axis]);
            point = point.map((value, axis) => value + direction[axis] * explode * extent * 0.4);
            return rotate(point.map((value, axis) => value - center[axis]), cameraRotation);
          });
          const firstEdge = transformed[1].map((value, axis) => value - transformed[0][axis]);
          const secondEdge = transformed[2].map((value, axis) => value - transformed[0][axis]);
          const normal = [
            firstEdge[1] * secondEdge[2] - firstEdge[2] * secondEdge[1],
            firstEdge[2] * secondEdge[0] - firstEdge[0] * secondEdge[2],
            firstEdge[0] * secondEdge[1] - firstEdge[1] * secondEdge[0]
          ];
          const normalLength = Math.hypot(...normal) || 1;
          const light = [0.36, 0.52, 0.77];
          const diffuse = Math.max(0, normal.reduce((sum, value, axis) =>
            sum + value / normalLength * light[axis], 0));
          const shade = 0.34 + diffuse * 0.66;
          const litColor = color.map((value, axis) => axis < 3 ? Math.min(1, value * shade) : value);
          const screenPoints = [];
          for (const point of transformed) {
            const viewportScale = 1.52 * Math.min(width, height);
            const clipX = point[0] / extent * zoom * viewportScale / width;
            const clipY = point[1] / extent * zoom * viewportScale / height;
            const clipZ = Math.max(-0.99, Math.min(0.99, point[2] / extent));
            vertices.push(clipX, clipY, clipZ, ...litColor);
            screenPoints.push([(clipX + 1) * width / 2, (1 - clipY) * height / 2, clipZ]);
          }
          nextPickTriangles.push({ body: part.body, points: screenPoints,
            depth: screenPoints.reduce((sum, point) => sum + point[2], 0) / 3 });
        }
      }
      pickTriangles = nextPickTriangles;
      if (selected.length === 2 && centers.has(selected[0]) && centers.has(selected[1])) {
        const first = centers.get(selected[0]); const second = centers.get(selected[1]);
        const distance = Math.hypot(...first.map((value, axis) => value - second[axis]));
        status.textContent = `${selected[0]} ↔ ${selected[1]} centroid distance: ${distance.toFixed(3)} mm`;
      } else status.textContent = selected.length ? `Selected: ${selected.join(", ")}` : "Select up to two components to measure";
      const background = scene?.background === "NIGHT" ? [0.018, 0.027, 0.043, 1] : [0.075, 0.09, 0.115, 1];
      gl.clearColor(...background); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT); gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE);
      gl.useProgram(program); gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.enableVertexAttribArray(positionLocation); gl.vertexAttribPointer(positionLocation, 3, gl.FLOAT, false, 28, 0);
      gl.enableVertexAttribArray(colorLocation); gl.vertexAttribPointer(colorLocation, 4, gl.FLOAT, false, 28, 12);
      const ground = [];
      const viewportScale = 1.52 * Math.min(width, height);
      for (let line = -10; line <= 10; line += 1) {
        const offset = extent * 0.075 * line;
        for (const point of [[-extent * 0.75, offset, -extent * 0.52],
          [extent * 0.75, offset, -extent * 0.52], [offset, -extent * 0.75, -extent * 0.52],
          [offset, extent * 0.75, -extent * 0.52]]) {
          const projected = rotate(point, cameraRotation);
          ground.push(projected[0] / extent * zoom * viewportScale / width,
            projected[1] / extent * zoom * viewportScale / height,
            Math.max(-0.99, Math.min(0.99, projected[2] / extent)), 0.25, 0.29, 0.34, 1);
        }
      }
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(ground), gl.DYNAMIC_DRAW);
      gl.drawArrays(gl.LINES, 0, ground.length / 7);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(vertices), gl.DYNAMIC_DRAW);
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
    const selected = [];
    let sceneIndex = 0;
    let playing = false;
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
    play.textContent = "Play";
    play.onclick = () => {
      playing = !playing;
      play.textContent = playing ? "Pause" : "Play";
      started = performance.now();
      if (playing) { emittedEvents.clear(); requestAnimationFrame(draw); }
    };
    controls.appendChild(play);
    root.parentElement.insertBefore(controls, root);

    const componentDrop = addDropMenu(root.parentElement, "Components", "icad-component-menu");
    const componentStatus = document.createElement("output");
    componentStatus.className = "icad-measurement";
    componentDrop.content.appendChild(componentStatus);
    const componentButtons = new Map();
    const toggleSelection = body => {
      const found = selected.indexOf(body);
      if (found >= 0) selected.splice(found, 1);
      else { selected.push(body); if (selected.length > 2) selected.shift(); }
      for (const [name, button] of componentButtons)
        button.setAttribute("aria-pressed", String(selected.includes(name)));
      draw(performance.now());
    };
    for (const body of [...new Set(model.parts.map(part => part.body))]) {
      const item = document.createElement("button");
      item.textContent = body; item.setAttribute("aria-pressed", "false");
      item.onclick = () => toggleSelection(body);
      componentButtons.set(body, item); componentDrop.content.appendChild(item);
    }
    const sceneDrop = addDropMenu(root.parentElement, "Scenes", "icad-scene-menu");
    if (model.scenes.length === 0) {
      const empty = document.createElement("span"); empty.textContent = "Static model";
      sceneDrop.content.appendChild(empty);
    }
    model.scenes.forEach((sceneValue, index) => {
      const item = document.createElement("button"); item.textContent = `▶ ${sceneValue.name}`;
      item.onclick = () => { sceneIndex = index; playing = true; started = performance.now(); emittedEvents.clear(); play.textContent = "Pause"; requestAnimationFrame(draw); sceneDrop.menu.open = false; };
      sceneDrop.content.appendChild(item);
    });
    addViewCube(root.parentElement, rotation => { viewRotation = rotation; draw(performance.now()); });

    let drag;
    let pointerStart;
    let pickTriangles = [];
    root.addEventListener("pointerdown", (event) => { drag = [event.clientX, event.clientY]; pointerStart = drag.slice(); root.setPointerCapture(event.pointerId); });
    root.addEventListener("pointermove", (event) => {
      if (!drag) return;
      viewRotation[2] += (event.clientX - drag[0]) * 0.35;
      viewRotation[0] += (event.clientY - drag[1]) * 0.35;
      drag = [event.clientX, event.clientY];
    });
    root.addEventListener("pointerup", event => {
      const clicked = pointerStart && Math.hypot(event.clientX - pointerStart[0], event.clientY - pointerStart[1]) < 4;
      drag = undefined;
      if (!clicked) return;
      const bounds = root.getBoundingClientRect();
      const x = event.clientX - bounds.left; const y = event.clientY - bounds.top;
      const hit = [...pickTriangles].reverse().find(candidate => pointInTriangle(x, y, candidate.points));
      if (hit) toggleSelection(hit.body);
    });
    root.addEventListener("wheel", (event) => {
      event.preventDefault();
      zoom = Math.max(0.2, Math.min(6, zoom * Math.exp(-event.deltaY * 0.001)));
    }, { passive: false });

    const { center, extent } = modelBounds(model);

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
      const background = scene && scene.background === "NIGHT" ? "#050914" : "#131720";
      context.fillStyle = background;
      context.fillRect(0, 0, width, height);
      context.strokeStyle = "rgba(148,163,184,.18)";
      context.lineWidth = 1;
      for (let line = -10; line <= 10; line += 1) {
        const offset = line * Math.min(width, height) / 24;
        context.beginPath(); context.moveTo(width * 0.15, height * 0.62 + offset * 0.38);
        context.lineTo(width * 0.85, height * 0.62 + offset * 0.38); context.stroke();
        context.beginPath(); context.moveTo(width / 2 + offset, height * 0.28);
        context.lineTo(width / 2 + offset, height * 0.9); context.stroke();
      }

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

      const scale = 0.76 * Math.min(width, height) / Math.max(extent, 1) * zoom;
      const triangles = [];

      for (const part of model.parts) {
        if (visibility.get(part.body) === false) continue;
        const transform = transforms.get(part.body) || { position: [0, 0, 0], rotation: [0, 0, 0] };
        const partCenter = [0, 1, 2].map(axis => part.vertices.reduce(
          (sum, point) => sum + point[axis], 0) / part.vertices.length);
        const projected = part.vertices.map((vertex) => {
          let point = animateOccurrence(vertex.slice(), part.body);
          point = rotate(point.map((value, index) => value - partCenter[index]), transform.rotation)
            .map((value, index) => value + partCenter[index] + transform.position[index]);
          point = rotate(point.map((value, index) => value - center[index]), cameraRotation);
          return [width / 2 + point[0] * scale, height / 2 - point[1] * scale, point[2]];
        });
        for (const face of part.triangles) {
          const points = face.map((index) => projected[index]);
          const firstEdge = points[1].map((value, axis) => value - points[0][axis]);
          const secondEdge = points[2].map((value, axis) => value - points[0][axis]);
          const normalZ = firstEdge[0] * secondEdge[1] - firstEdge[1] * secondEdge[0];
          triangles.push({ points, body: part.body,
            depth: points.reduce((sum, point) => sum + point[2], 0) / 3,
            material: part.material, shade: 0.5 + Math.min(0.45, Math.abs(normalZ) / 6000) });
        }
      }
      triangles.sort((a, b) => a.depth - b.depth);
      pickTriangles = triangles;
      componentStatus.textContent = selected.length ? `Selected: ${selected.join(", ")}` :
        "Click geometry or choose up to two components";
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
        context.globalAlpha = 1 - triangle.shade;
        context.fillStyle = "#000";
        context.fill();
        context.globalAlpha = 1;
        context.strokeStyle = selected.includes(triangle.body) ? "#fbbf24" : "rgba(226,232,240,.18)";
        context.lineWidth = selected.includes(triangle.body) ? 1.5 : 0.5;
        context.stroke();
      }
      context.fillStyle = "#dbeafe";
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
