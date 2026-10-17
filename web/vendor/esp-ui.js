'use strict';

window.EspUi = Object.freeze({
  byId(id) {
    return document.getElementById(id);
  },
  formatBytes(value) {
    return value >= 1048576
      ? `${(value / 1048576).toFixed(1)} MiB`
      : `${Math.round(value / 1024)} KiB`;
  },
  formatDuration(milliseconds) {
    const seconds = Math.floor(milliseconds / 1000);
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor(seconds % 3600 / 60);
    return `${hours}h ${minutes}m ${seconds % 60}s`;
  },
  setConnection(element, online, labels = {}) {
    element.textContent = online
      ? (labels.online || '● Online')
      : (labels.offline || '● Offline');
    element.classList.toggle('online', online);
    element.classList.toggle('offline', !online);
  },
});
