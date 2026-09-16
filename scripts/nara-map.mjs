// Pure mapping from a Nara CSV row (object keyed by header) to an `entries` row.
// No I/O here so it can be unit-tested against the real export.

const TYPE_MAP = {
  Breastfeed: 'breastfeed',
  'Bottle Feed': 'bottle',
  'Combo Feed': 'combo',
  Diaper: 'diaper',
  Pump: 'pump',
  Growth: 'growth'
};

const TEXTURE_MAP = { RUN: 'runny', MUCOUS: 'mucousy', MUSH: 'mushy', SOLID: 'solid', PEBBLE: 'pebbles' };
const COLORS = new Set(['black', 'green', 'yellow', 'brown', 'red', 'gray']);

const num = (v) => (v === undefined || v === null || v === '' ? null : Number(v));
const int = (v) => (v === undefined || v === null || v === '' ? 0 : Math.round(Number(v)));

function assertUnit(row, col, expected) {
  const u = row[col];
  if (u && u !== expected) throw new Error(`Unexpected unit ${u} in ${col} for ${row._activityKey}`);
}

function side(v) {
  if (!v) return { side: null, manual: false };
  const manual = v.endsWith('.nonTimer');
  const s = v.replace('.nonTimer', '').toLowerCase();
  if (s !== 'left' && s !== 'right') throw new Error(`Unexpected side ${v}`);
  return { side: s, manual };
}

function breastfeedPayload(row, prefix) {
  const b = side(row[`[${prefix}] Begin Side`]);
  const e = side(row[`[${prefix}] End Side`]);
  const left_s = int(row[`[${prefix}] Left Duration (Seconds)`]);
  const right_s = int(row[`[${prefix}] Right Duration (Seconds)`]);
  return {
    payload: { begin_side: b.side, end_side: e.side, left_s, right_s, manual: b.manual || e.manual, segments: [] },
    duration_s: left_s + right_s
  };
}

function bottlePayload(row, prefix) {
  const t = row[`[${prefix}] Type`] || '';
  const kinds = [];
  if (/Breast Milk/.test(t)) kinds.push('breast_milk');
  if (/Formula/.test(t)) kinds.push('formula');
  assertUnit(row, `[${prefix}] Breast Milk Volume Unit`, 'ML');
  assertUnit(row, `[${prefix}] Formula Volume Unit`, 'ML');
  assertUnit(row, `[${prefix}] Volume Unit`, 'ML');
  let breast_milk_ml = num(row[`[${prefix}] Breast Milk Volume`]);
  let formula_ml = num(row[`[${prefix}] Formula Volume`]);
  const plain = num(row[`[${prefix}] Volume`]);
  // Defensive: if only the generic Volume column is filled, attribute it to the single kind.
  if (plain != null && breast_milk_ml == null && formula_ml == null && kinds.length === 1) {
    if (kinds[0] === 'formula') formula_ml = plain; else breast_milk_ml = plain;
  }
  const payload = { kinds };
  if (breast_milk_ml != null) payload.breast_milk_ml = breast_milk_ml;
  if (formula_ml != null) payload.formula_ml = formula_ml;
  const brand = row[`[${prefix}] Formula Name`];
  if (brand) payload.formula_brand = brand;
  return payload;
}

function diaperPayload(row) {
  const t = row['[Diaper] Type'] || '';
  const detail = row['[Diaper] Detail'] || '';
  const wet = /Wet/.test(t);
  const dirty = /Dirty/.test(t);
  const dry = /Dry/.test(t);
  if (!wet && !dirty && !dry) throw new Error(`Unexpected diaper type ${t}`);
  const texture = (row['[Diaper] Dirty Texture'] || '')
    .split(/\s+/).filter(Boolean)
    .map((tok) => {
      const m = TEXTURE_MAP[tok];
      if (!m) throw new Error(`Unexpected texture ${tok}`);
      return m;
    });
  const color = (row['[Diaper] Dirty Color'] || '')
    .split(/\s+/).filter(Boolean)
    .map((tok) => {
      const c = tok.toLowerCase();
      if (!COLORS.has(c)) throw new Error(`Unexpected color ${tok}`);
      return c;
    });
  return { wet, dirty, dry, texture, color, blowout: /Blowout/.test(detail), rash: /Rash/.test(detail) };
}

function pumpPayload(row) {
  assertUnit(row, '[Pump] Left Volume Unit', 'ML');
  assertUnit(row, '[Pump] Right Volume Unit', 'ML');
  const payload = {};
  const l = num(row['[Pump] Left Volume']);
  const r = num(row['[Pump] Right Volume']);
  if (l != null) payload.left_ml = l;
  if (r != null) payload.right_ml = r;
  return payload;
}

function growthPayload(row) {
  const payload = {};
  const w = num(row['[Growth] Weight']);
  if (w != null) {
    const u = row['[Growth] Weight Unit'];
    if (u === 'LB') payload.weight_kg = Math.round(w * 0.45359237 * 1000) / 1000;
    else if (u === 'KG') payload.weight_kg = w;
    else throw new Error(`Unexpected weight unit ${u}`);
  }
  const h = num(row['[Growth] Height']);
  if (h != null) {
    const u = row['[Growth] Height Unit'];
    if (u === 'CM') payload.height_cm = h;
    else if (u === 'IN') payload.height_cm = Math.round(h * 2.54 * 10) / 10;
    else throw new Error(`Unexpected height unit ${u}`);
  }
  const hd = num(row['[Growth] Head Size']);
  if (hd != null) {
    const u = row['[Growth] Head Size Unit'];
    if (u === 'CM') payload.head_cm = hd;
    else if (u === 'IN') payload.head_cm = Math.round(hd * 2.54 * 10) / 10;
    else throw new Error(`Unexpected head unit ${u}`);
  }
  return payload;
}

/**
 * @param {Record<string,string>} row
 * @param {{ household_id: string, child_id: string, caregivers: Record<string,string> }} ctx
 *   caregivers maps Nara display name → caregiver user_id
 * @returns {object|null} an `entries` row, or null when the row type is out of scope
 */
export function mapRow(row, ctx) {
  const type = TYPE_MAP[row.Type];
  if (!type) return null;
  const epoch = row['Start Date/time (Epoch)'];
  if (!epoch) throw new Error(`Missing start epoch for ${row._activityKey}`);
  const started = new Date(Number(epoch));
  let ended = null;
  let payload;

  switch (type) {
    case 'breastfeed': {
      const r = breastfeedPayload(row, 'Breastfeed');
      payload = r.payload;
      ended = new Date(started.getTime() + r.duration_s * 1000);
      break;
    }
    case 'combo': {
      const r = breastfeedPayload(row, 'Combo Feed');
      payload = { ...r.payload, ...bottlePayload(row, 'Combo Feed') };
      ended = new Date(started.getTime() + r.duration_s * 1000);
      break;
    }
    case 'bottle':
      payload = bottlePayload(row, 'Bottle Feed');
      break;
    case 'diaper':
      payload = diaperPayload(row);
      break;
    case 'pump': {
      payload = pumpPayload(row);
      const endEpoch = row['[Pump] End Date/time (Epoch)'];
      if (endEpoch) ended = new Date(Number(endEpoch));
      else if (row['[Pump] Duration (Seconds)']) ended = new Date(started.getTime() + int(row['[Pump] Duration (Seconds)']) * 1000);
      else ended = started;
      break;
    }
    case 'growth':
      payload = growthPayload(row);
      break;
  }

  const who = (name) => {
    if (!name) return null;
    const id = ctx.caregivers[name];
    if (!id) throw new Error(`Unknown caregiver ${name}`);
    return id;
  };

  return {
    household_id: ctx.household_id,
    child_id: type === 'pump' ? null : ctx.child_id,
    type,
    started_at: started.toISOString(),
    ended_at: ended ? ended.toISOString() : null,
    payload,
    note: row.Note || null,
    created_by: who(row['Created By Caregiver']),
    updated_by: who(row['Last Updated By Caregiver']),
    nara_activity_key: row._activityKey
  };
}
