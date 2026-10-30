const statusConfig = {
  clear: {
    label: 'System stable',
    detail: 'All zones reporting. Community response is ready.',
    fill: 92,
    accent: 'green'
  },
  watch: {
    label: 'Watch mode',
    detail: 'Movement detected near the training courtyard. Staff remain alert.',
    fill: 68,
    accent: 'amber'
  },
  alert: {
    label: 'Alert broadcast',
    detail: 'Safety response protocol engaged. Evacuation route prioritized.',
    fill: 34,
    accent: 'red'
  }
};

const zoneConfig = {
  'Dandora Market': {
    badge: 'Monitoring',
    note: 'Foot traffic is high, and patrol teams are ready to respond to suspicious activity.',
    readiness: 89,
    response: '2.4s'
  },
  'Mugoya Road': {
    badge: 'Standby',
    note: 'Road activity is being tracked and dispatch teams remain prepared for rapid intervention.',
    readiness: 76,
    response: '3.1s'
  },
  'Kangundo Road': {
    badge: 'Escalated',
    note: 'High alert coverage is active as emergency teams prepare for a rapid response route.',
    readiness: 55,
    response: '1.8s'
  }
};

const statusLabel = document.getElementById('status-label');
const statusDetail = document.getElementById('status-detail');
const readinessFill = document.getElementById('readiness-fill');
const statusDot = document.querySelector('.signal-dot');
const statusButtons = document.querySelectorAll('.status-btn');

function setStatus(level) {
  const config = statusConfig[level];
  if (!config) return;

  statusLabel.textContent = config.label;
  statusDetail.textContent = config.detail;
  readinessFill.style.width = `${config.fill}%`;
  readinessFill.dataset.level = config.accent;
  statusDot.dataset.level = config.accent;

  statusButtons.forEach((button) => {
    const active = button.dataset.level === level;
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', active ? 'true' : 'false');
  });
}

statusButtons.forEach((button) => {
  button.addEventListener('click', () => setStatus(button.dataset.level));
});

const zoneTitle = document.getElementById('zone-title');
const zoneBadge = document.getElementById('zone-badge');
const zoneNote = document.getElementById('zone-note');
const zoneReadiness = document.getElementById('zone-readiness');
const zoneResponse = document.getElementById('zone-response');
const zoneButtons = document.querySelectorAll('.zone-btn');

function setZone(zoneName) {
  const config = zoneConfig[zoneName];
  if (!config) return;

  zoneTitle.textContent = zoneName;
  zoneBadge.textContent = config.badge;
  zoneNote.textContent = config.note;
  zoneReadiness.textContent = `${config.readiness}%`;
  zoneResponse.textContent = config.response;

  zoneButtons.forEach((button) => {
    const active = button.dataset.zone === zoneName;
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', active ? 'true' : 'false');
  });
}

zoneButtons.forEach((button) => {
  button.addEventListener('click', () => setZone(button.dataset.zone));
});

setStatus('clear');
setZone('Dandora Market');

const liveStats = {
  alerts: { value: 284, suffix: '', step: 2, decimals: 0 },
  resolved: { value: 76, suffix: '%', step: 1, decimals: 0 },
  dispatch: { value: 2.4, suffix: 's', step: 0.1, decimals: 1 },
  priority: { value: 12, suffix: '', step: 1, decimals: 0 }
};

function animateLiveStat(el) {
  const key = el.dataset.liveStat;
  const config = liveStats[key];
  if (!config) return;

  const target = config.value;
  const suffix = config.suffix;
  const decimals = config.decimals;
  let current = 0;

  const tick = () => {
    current = Math.min(current + config.step, target);
    const formatted = Number(current).toFixed(decimals);
    el.querySelector('strong').textContent = `${formatted}${suffix}`;

    if (current >= target) {
      clearInterval(timer);
      return;
    }
  };

  const timer = setInterval(tick, 160);
  tick();
}

const liveStatCards = document.querySelectorAll('[data-live-stat]');
liveStatCards.forEach((card) => animateLiveStat(card));

setInterval(() => {
  const alertsCard = document.querySelector('[data-live-stat="alerts"] strong');
  const resolvedCard = document.querySelector('[data-live-stat="resolved"] strong');
  const dispatchCard = document.querySelector('[data-live-stat="dispatch"] strong');
  const priorityCard = document.querySelector('[data-live-stat="priority"] strong');

  if (alertsCard) {
    const next = Number(alertsCard.textContent.replace(/[^\d]/g, '')) + 1;
    alertsCard.textContent = `${Math.min(next, 320)}`;
  }

  if (resolvedCard) {
    const next = Math.min(Number(resolvedCard.textContent.replace(/[^\d]/g, '')) + 1, 91);
    resolvedCard.textContent = `${next}%`;
  }

  if (dispatchCard) {
    const next = Math.max(2.1, Number(dispatchCard.textContent.replace(/[^\d.]/g, '')) - 0.1);
    dispatchCard.textContent = `${next.toFixed(1)}s`;
  }

  if (priorityCard) {
    const next = Math.min(Number(priorityCard.textContent.replace(/[^\d]/g, '')) + 1, 18);
    priorityCard.textContent = `${next}`;
  }
}, 5000);

const liveClock = document.getElementById('live-clock');

function updateClock() {
  if (!liveClock) return;

  const now = new Date();
  liveClock.textContent = new Intl.DateTimeFormat('en-US', {
    hour: 'numeric',
    minute: '2-digit',
    second: '2-digit',
    hour12: true
  }).format(now);
}

updateClock();
setInterval(updateClock, 1000);

const counterElements = document.querySelectorAll('[data-target]');

counterElements.forEach((counter) => {
  const targetValue = counter.dataset.target;
  if (!targetValue || counter.dataset.suffix === '+') {
    return;
  }

  const target = Number(targetValue || 0);
  let value = 0;
  const increment = Math.max(1, Math.ceil(target / 45));

  const timer = setInterval(() => {
    value += increment;
    if (value >= target) {
      counter.textContent = `${target}${counter.dataset.suffix || ''}`;
      clearInterval(timer);
      return;
    }
    counter.textContent = `${value}${counter.dataset.suffix || ''}`;
  }, 35);
});

const scrollProgressBar = document.querySelector('.scroll-progress-bar');
const revealElements = document.querySelectorAll('[data-reveal]');

function updateScrollProgress() {
  const scrollTop = window.scrollY;
  const docHeight = document.documentElement.scrollHeight - window.innerHeight;
  const progress = docHeight > 0 ? (scrollTop / docHeight) * 100 : 0;

  if (scrollProgressBar) {
    scrollProgressBar.style.width = `${Math.min(progress, 100)}%`;
  }
}

function revealOnScroll() {
  revealElements.forEach((element) => {
    const rect = element.getBoundingClientRect();
    const revealPoint = window.innerHeight * 0.82;

    if (rect.top < revealPoint) {
      element.classList.add('visible');
    }
  });
}

window.addEventListener('scroll', () => {
  updateScrollProgress();
  revealOnScroll();
});

window.addEventListener('load', () => {
  updateScrollProgress();
  revealOnScroll();
});
