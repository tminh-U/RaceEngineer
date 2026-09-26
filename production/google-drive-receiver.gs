// Deploy as a Web app: Execute as me, Who has access: Anyone.
// Script properties: RACEENGINEER_SHARE_TOKEN and RACEENGINEER_FOLDER_ID.
const MAX_BYTES = 20 * 1024 * 1024;
const FIELDS = new Set([
  'share_schema_version', 'record_type', 'session_id', 'captured_utc',
  'simulator', 'completed_lap', 'lap_time_s', 'sample_current_lap',
  'in_pit_at_sample', 'lap_excluded', 'track', 'car_model', 'car_category',
  'car_subclass', 'total_laps', 'fuel_used_l', 'fuel_at_sample_l',
  'gap_ahead_at_sample_s', 'gap_behind_at_sample_s', 'current_lap',
  'pit_state', 'realism_confirmed', 'tyre_wear_0_at_sample', 'tyre_wear_1_at_sample',
  'tyre_wear_2_at_sample', 'tyre_wear_3_at_sample',
  'tyre_temp_0_at_sample', 'tyre_temp_1_at_sample',
  'tyre_temp_2_at_sample', 'tyre_temp_3_at_sample'
]);
const RECORD_TYPES = new Set(['lap', 'pit_enter', 'pit_exit', 'pit_box_enter', 'pit_box_exit']);

function response(accepted, error) {
  return ContentService.createTextOutput(JSON.stringify({accepted, error: error || ''}))
    .setMimeType(ContentService.MimeType.JSON);
}

function validRow(row) {
  if (!row || typeof row !== 'object' || Array.isArray(row)
      || row.share_schema_version !== 1 || !RECORD_TYPES.has(row.record_type)
      || row.realism_confirmed !== true
      || !['sim_ac', 'sim_acc'].includes(row.simulator)
      || typeof row.session_id !== 'string'
      || !/^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(row.session_id)
      || typeof row.captured_utc !== 'string' || !row.captured_utc.startsWith('2000-')) return false;
  return Object.keys(row).every(key => FIELDS.has(key) && (
    (typeof row[key] === 'string' && row[key].length <= 256)
    || typeof row[key] === 'boolean'
    || (typeof row[key] === 'number' && Number.isFinite(row[key]))));
}

function doGet() {
  return ContentService.createTextOutput('RaceEngineer receiver');
}

function doPost(e) {
  try {
    if (!e || !e.postData || e.contentLength < 1 || e.contentLength > MAX_BYTES + 513)
      return response(false, 'too_large');
    const body = e.postData.contents;
    const separator = body.indexOf('\n');
    if (separator < 0 || separator > 512) return response(false, 'invalid');
    const properties = PropertiesService.getScriptProperties();
    const expectedToken = properties.getProperty('RACEENGINEER_SHARE_TOKEN');
    if (!expectedToken || expectedToken.length < 32 || body.slice(0, separator) !== expectedToken)
      return response(false, 'unauthorized');
    const data = body.slice(separator + 1);
    if (!data || Utilities.newBlob(data).getBytes().length > MAX_BYTES)
      return response(false, 'too_large');
    let sessionId = '';
    let count = 0;
    for (const line of data.split('\n')) {
      if (!line) continue;
      const row = JSON.parse(line);
      if (!validRow(row) || (sessionId && sessionId !== row.session_id))
        return response(false, 'invalid');
      sessionId = row.session_id;
      count++;
    }
    if (!count) return response(false, 'invalid');
    const folderId = properties.getProperty('RACEENGINEER_FOLDER_ID');
    if (!folderId) return response(false, 'not_configured');
    const hash = Utilities.computeDigest(Utilities.DigestAlgorithm.SHA_256, data,
      Utilities.Charset.UTF_8).map(byte => (byte & 255).toString(16).padStart(2, '0')).join('');
    const name = `pit_strategy_laps-${hash}.jsonl`;
    const lock = LockService.getScriptLock();
    if (!lock.tryLock(10000)) return response(false, 'busy');
    try {
      const folder = DriveApp.getFolderById(folderId);
      if (!folder.getFilesByName(name).hasNext())
        folder.createFile(Utilities.newBlob(data, 'application/x-ndjson', name));
    } finally {
      lock.releaseLock();
    }
    return response(true);
  } catch (error) {
    console.error(error);
    return response(false, 'storage');
  }
}
