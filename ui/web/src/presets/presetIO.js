(function initPresetIO(global) {
  function downloadObjectAsJson(payload, fileName) {
    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = fileName;
    anchor.click();
    URL.revokeObjectURL(url);
  }

  async function readJsonFile(file) {
    const text = await file.text();
    return JSON.parse(text);
  }

  global.BubbleCloudPresetIO = { downloadObjectAsJson, readJsonFile };
})(window);
