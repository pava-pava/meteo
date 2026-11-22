'use strict';

import * as fs from 'fs';

const slots = 28801;
const recordSize = 32;
const batchSize = 128;
const retentionMs = 10 * 86400000;
const fields = [
	['temp_bme_c', -100, 200, 100],
	['temp_ds_c', -100, 200, 100],
	['humidity_pct', 0, 100, 100],
	['pressure_mmhg', 0, 1500, 10],
	['co2_ppm', 0, 65535, 1]
];

function nowMs() {
	const t = clock();
	return t[0] * 1000 + int(t[1] / 1000000);
}

function littleEndian(value, width) {
	let result = '';
	for (let i = 0; i < width; i++)
		result += chr((value >> (8 * i)) & 255);
	return result;
}

function numberAt(data, offset, width) {
	let value = 0;
	for (let i = 0; i < width; i++)
		value |= ord(data, offset + i) << (8 * i);
	return value;
}

function bootBytes(boot) {
	let result = '';
	for (let i = 14; i >= 0; i -= 2)
		result += chr(int(substr(boot, i, 2), 16));
	return result;
}

function decodeBoot(bytes) {
	let value = '';
	for (let i = 15; i >= 8; i--)
		value += sprintf('%02x', ord(bytes, i));
	return value;
}

function nextIndex(cursor, offset) {
	return (cursor.Head + offset) % slots;
}

function writeJson(path, value) {
	const contents = sprintf('%J\n', value);
	if (fs.writefile(path, contents) !== length(contents))
		die('Cannot write queue file');
}

function openQueue(dir) {
	const cursorPath = dir + '/cursor.json';
	const dataPath = dir + '/samples.bin';
	const cursor = fs.stat(cursorPath)
		? json(fs.readfile(cursorPath))
		: { Head: 0, Count: 0 };
	if (type(cursor.Head) !== 'int' || cursor.Head < 0 || cursor.Head >= slots ||
	    type(cursor.Count) !== 'int' || cursor.Count < 0 || cursor.Count >= slots)
		die('Invalid queue cursor');
	const info = fs.stat(dataPath);
	let file;
	if (info && info.size === slots * recordSize) {
		file = fs.open(dataPath, 'r+');
	} else {
		if (cursor.Count)
			die('Missing or invalid queue file');
		file = fs.open(dataPath, 'w+', 384);
		if (!file || !file.seek(slots * recordSize - 1) || file.write(chr(0)) !== 1 || !file.flush())
			die('Cannot initialize queue');
	}
	if (!file)
		die('Cannot open queue');
	return { file, cursor, cursorPath };
}

function save(queue) {
	const next = queue.cursorPath + '.next';
	const state = sprintf('{"Head":%d,"Count":%d}\n', queue.cursor.Head, queue.cursor.Count);
	if (fs.writefile(next, state) !== length(state))
		die('Cannot write queue cursor');
	if (!fs.rename(next, queue.cursorPath))
		die('Cannot commit queue cursor');
}

function readRecord(queue, index) {
	if (!queue.file.seek(index * recordSize))
		die('Cannot seek queue');
	const data = queue.file.read(recordSize);
	if (length(data) !== recordSize)
		die('Cannot read queue record');
	return data;
}

function expire(queue, now) {
	const cursor = queue.cursor;
	const before = cursor.Count;
	while (cursor.Count > 0 && numberAt(readRecord(queue, cursor.Head), 0, 8) < now - retentionMs) {
		cursor.Head = nextIndex(cursor, 1);
		cursor.Count--;
	}
	if (cursor.Count !== before)
		save(queue);
}

function pack(sample, station, now) {
	if (type(sample) !== 'object' || sample.station !== station ||
	    type(sample.boot) !== 'string' || !match(sample.boot, /^[0-9a-f]{16}$/) ||
	    sample.boot === '0000000000000000' ||
	    type(sample.seq) !== 'int' || sample.seq < 1 || sample.seq > 4294967295)
		return null;
	for (let key in keys(sample)) {
		if (key !== 'station' && key !== 'boot' && key !== 'seq' &&
		    !length(filter(fields, (field) => field[0] === key)))
			return null;
	}
	let data = littleEndian(now, 8) + bootBytes(sample.boot) + littleEndian(sample.seq, 4);
	let mask = 0;
	for (let i = 0; i < length(fields); i++) {
		const field = fields[i];
		const value = sample[field[0]];
		let encoded = 0;
		if (value !== null) {
			const valueType = type(value);
			if ((valueType !== 'int' && valueType !== 'double') ||
			    !(value >= field[1] && value <= field[2]) || (i === 4 && valueType !== 'int'))
				return null;
			mask |= 1 << i;
			encoded = int(value * field[3] + (value < 0 ? -0.5 : 0.5));
		}
		data += littleEndian(encoded, 2);
	}
	return data + littleEndian(mask, 2);
}

function unpack(data, station) {
	const sample = {
		station,
		boot: decodeBoot(data),
		seq: numberAt(data, 16, 4),
		measured_at_ms: numberAt(data, 0, 8)
	};
	const mask = numberAt(data, 30, 2);
	for (let i = 0; i < length(fields); i++) {
		let value = numberAt(data, 20 + i * 2, 2);
		if (i < 2 && value >= 32768)
			value -= 65536;
		sample[fields[i][0]] = null;
		if (mask & (1 << i))
			sample[fields[i][0]] = i === 4 ? value : value / (fields[i][3] + 0.0);
	}
	return sample;
}

function append(queue, data) {
	const cursor = queue.cursor;
	const index = nextIndex(cursor, cursor.Count);
	if (cursor.Count > 0 && substr(readRecord(queue, nextIndex(cursor, cursor.Count - 1)), 8, 12) === substr(data, 8, 12))
		return;
	// Write into a spare slot before committing the cursor. This keeps the old queue valid if interrupted.
	if (!queue.file.seek(index * recordSize) || queue.file.write(data) !== recordSize || !queue.file.flush())
		die('Cannot append queue record');
	if (cursor.Count === slots - 1)
		cursor.Head = nextIndex(cursor, 1);
	else
		cursor.Count++;
	save(queue);
}

function http(status) {
	print('Status: ' + status + '\r\nContent-Type: text/plain\r\n\r\n');
}

function ingest(settings) {
	if (getenv('REQUEST_METHOD') !== 'POST') {
		http('405 Method Not Allowed');
		return;
	}
	const token = trim(fs.readfile(settings.tokenFile));
	if (length(token) < 16 || getenv('HTTP_AUTHORIZATION') !== 'Bearer ' + token) {
		http('401 Unauthorized');
		return;
	}
	const n = int(getenv('CONTENT_LENGTH'));
	if (n <= 0 || n > 1024) {
		http(n > 1024 ? '413 Payload Too Large' : '400 Bad Request');
		return;
	}
	const now = nowMs();
	if (now < 1735689600000) {
		http('503 Service Unavailable');
		return;
	}
	let data;
	try {
		const body = fs.stdin.read(n);
		if (length(body) === n)
			data = pack(json(body), settings.station, now);
	} catch (e) {
		http('400 Bad Request');
		return;
	}
	if (!data) {
		http('400 Bad Request');
		return;
	}
	try {
		const queue = openQueue(settings.spool);
		expire(queue, now);
		append(queue, data);
		queue.file.close();
	} catch (e) {
		warn('meteo queue: ' + e + '\n');
		http('503 Service Unavailable');
		return;
	}
	http('204 No Content');
}

const mode = ARGV[0];
const settings = json(fs.readfile(ARGV[1]));
if (mode === 'ingest') {
	ingest(settings);
} else {
	const queue = openQueue(settings.spool);
	const cursor = queue.cursor;
	const work = ARGV[2];
	if (mode === 'batch') {
		expire(queue, nowMs());
		const payload = [];
		for (let i = 0; i < min(cursor.Count, batchSize); i++)
			push(payload, unpack(readRecord(queue, nextIndex(cursor, i)), settings.station));
		if (length(payload)) {
			writeJson(work + '/batch.json', payload);
			const last = payload[length(payload) - 1];
			writeJson(work + '/last.json', { boot: last.boot, seq: last.seq });
		}
		print(length(payload));
	} else if (mode === 'ack') {
		const last = json(fs.readfile(work + '/last.json'));
		const id = bootBytes(last.boot) + littleEndian(last.seq, 4);
		for (let i = 0; i < min(cursor.Count, batchSize); i++) {
			const index = nextIndex(cursor, i);
			if (substr(readRecord(queue, index), 8, 12) === id) {
				cursor.Head = (index + 1) % slots;
				cursor.Count -= i + 1;
				save(queue);
				break;
			}
		}
	} else {
		die('Invalid queue command');
	}
	queue.file.close();
}
