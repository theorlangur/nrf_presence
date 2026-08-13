const { Buffer } = require('node:buffer');
const util = require('node:util');
const {Zcl} = require('zigbee-herdsman');
const {enumLookup
    ,text
    ,numeric
    ,deviceEndpoints
    ,deviceAddCustomCluster
    ,onOff
    ,binary
    ,occupancy
    ,temperature
    ,humidity
    ,co2
    ,setupConfigureForReading
    ,setupConfigureForReporting
} = require('zigbee-herdsman-converters/lib/modernExtend');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const constants = require('zigbee-herdsman-converters/lib/constants');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const utils = require('zigbee-herdsman-converters/lib/utils');
const globalStore = require('zigbee-herdsman-converters/lib/store');
const {logger} = require('zigbee-herdsman-converters/lib/logger');
const e = exposes.presets;
const eo = exposes.options;
const ea = exposes.access;

const NS = 'zhc:orlangur';

const LD2412_ERR = {
    0: 'Ok',
    1: 'Restart',
    2: 'ReloadConfig',
    3: 'FactoryReset',
    4: 'Bluetooth',
    5: 'SetBasicCfg',
    6: 'SetLightSenseCfg',
    7: 'SetEnergyThresholds',
    8: 'RunBackAnalysis',
    9: 'ConfigureCollectStatistics',
    10: 'SnapshotStatistics',
};

// 1. Cluster Definition using named Zcl constants
const hlkLD2412Cluster = {
    name: "hlkLD2412",
    ID: 0xFC82,
    manufacturerCode: null,
    attributes: {
        baseConfig: { name: "baseConfig", ID: 0x0000, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        swVer: { name: "swVer", ID: 0x0001, type: Zcl.DataType.CHAR_STR, read: true, write: false },
        bluetoothMac: { name: "bluetoothMac", ID: 0x0002, type: Zcl.DataType.OCTET_STR, read: true, write: false },
        stillEnergyThresholds: { name: "stillEnergyThresholds", ID: 0x0003, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        moveEnergyThresholds: { name: "moveEnergyThresholds", ID: 0x0004, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        lightLevel: { name: "lightLevel", ID: 0x0005, type: Zcl.DataType.UINT8, read: true, write: false },
        flags: { name: "flags", ID: 0x0006, type: Zcl.DataType.UINT8, read: true, write: true },
        statSampleWindow: { name: "statSampleWindow", ID: 0x0007, type: Zcl.DataType.UINT8, read: true, write: true },
        energyStatStill: { name: "energyStatStill", ID: 0x0008, type: Zcl.DataType.OCTET_STR, read: true, write: false },
        energyStatMove: { name: "energyStatMove", ID: 0x0009, type: Zcl.DataType.OCTET_STR, read: true, write: false },
        lightSense: { name: "lightSense", ID: 0x000a, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        bluetoothState: { name: "bluetoothState", ID: 0x000b, type: Zcl.DataType.BOOLEAN, read: true, write: true },
    },
    commands: {
        restart: { name: "restart", ID: 0x0001, parameters: [] },
        factoryReset: { name: "factoryReset", ID: 0x0002, parameters: [] },
        runBackAnalysis: { name: "runBackAnalysis", ID: 0x0003, parameters: [] },
        takeStatSnapshot: { name: "takeStatSnapshot", ID: 0x0004, parameters: [] },
    },
    commandsResponse: {},
};

// 2. Custom Status Cluster (shared)
const customStatusCluster = {
    name: "customStatus",
    ID: 0xfc80,
    attributes: {
        status1: { name: "status1", ID: 0x0000, type: Zcl.DataType.INT16 },
        status2: { name: "status2", ID: 0x0001, type: Zcl.DataType.INT16 },
        status3: { name: "status3", ID: 0x0002, type: Zcl.DataType.INT16 },
    },
    commands: {
        stop_watchdog_feeding: { name: "stop_watchdog_feeding", ID: 0x0001, parameters: [] },
        clear_coredump: { name: "clear_coredump", ID: 0x0002, parameters: [] },
    },
    commandsResponse: {},
};

const customStatus2Cluster = {
    name: "customStatus2",
    ID: 0xfc81,
    attributes: {
        status1: { name: "status1", ID: 0x0000, type: Zcl.DataType.INT32 },
        status2: { name: "status2", ID: 0x0001, type: Zcl.DataType.INT32 },
        status3: { name: "status3", ID: 0x0002, type: Zcl.DataType.INT32 },
        status4: { name: "status4", ID: 0x0003, type: Zcl.DataType.INT32 },
    },
    commands: {
    },
    commandsResponse: {},
};

const deviceControlCluster = {
    name: "devCtrl",
    ID: 0xfc83,
    attributes: {
        main_still_energy_analysis: { name: "main_still_energy_analysis", ID: 0x0000, type: Zcl.DataType.OCTET_STR, read: true },
        aux_still_energy_analysis: { name: "aux_still_energy_analysis", ID: 0x0001, type: Zcl.DataType.OCTET_STR, read: true },
    },
    commands: {
        start_analysis_for_presence: { name: "start_analysis_for_presence", ID: 0x0001, parameters: [] },
        start_analysis_for_absense: { name: "start_analysis_for_absense", ID: 0x0002, parameters: [] },
        stop_analysis: { name: "stop_analysis", ID: 0x0003, parameters: [] },
    },
    commandsResponse: {},
};

// 3. Converters
const fzLocal = {
    cluster: 'hlkLD2412',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const result = {};
        // const ep = meta.endpoint_name ? `_${meta.endpoint_name}` : ''; 
        const data = msg.data;// 1. Get the endpoint mapping from our device definition (e.g., { sensor_1: 1, sensor_2: 2 })

        const endpointMapping = model.endpoint ? model.endpoint(msg.device) : {};
        
        // 2. Find the string name ('sensor_1') that matches the incoming physical endpoint ID
        const epName = Object.keys(endpointMapping).find(key => endpointMapping[key] === msg.endpoint.ID);
        
        // 3. Create the suffix (e.g., '_sensor_1')
        const ep = epName ? `_${epName}` : '';

        meta.logger.info(`[hlkLD2412] Incoming readResponse/report on endpoint ${msg.endpoint.ID}`);
        meta.logger.info(`[hlkLD2412] Endpoint name resolved as: '${ep}'`);

        if (data.baseConfig !== undefined) {
            const buf = data.baseConfig;
            // meta.logger.info(`[hlkLD2412] Raw baseConfig buffer (length ${buf.length}): ${buf.toString('hex')});as: '${ep}'`);
            const distResRaw = buf.readUInt8(10);
            result[`base_config${ep}`] = {
                [`range_min${ep}`]: buf.readFloatLE(0),
                [`range_max${ep}`]: buf.readFloatLE(4),
                [`clear_delay${ep}`]: buf.readUInt16LE(8),
                [`distance_resolution${ep}`]: distResRaw === 3 ? '0.20' : (distResRaw === 1 ? '0.50' : '0.75'),
            }
        }

        if (data.swVer !== undefined) result[`sw_ver${ep}`] = data.swVer;
        if (data.bluetoothMac !== undefined) result[`bluetooth_mac${ep}`] = data.bluetoothMac.toString('hex').toUpperCase();
        if (data.lightLevel !== undefined) result[`light_level${ep}`] = data.lightLevel;
        if (data.statSampleWindow !== undefined) result[`statistics_sample_count_window${ep}`] = data.statSampleWindow;
        if (data.bluetoothState !== undefined) result[`bluetooth_state${ep}`] = data.bluetoothState === 1;

        if (data.flags !== undefined) {
            result[`background_analysis_active${ep}`] = (data.flags & (1 << 0)) > 0;
            result[`background_analysis_ok${ep}`] = (data.flags & (1 << 1)) > 0;
        }

        if (data.stillEnergyThresholds !== undefined) {
            const parsed = {};
            for (let i = 0; i < 14; i++) parsed[`gate_${i}${ep}`] = data.stillEnergyThresholds.readUInt8(i);
            result[`still_energy_thresholds${ep}`] = parsed;
        }

        if (data.moveEnergyThresholds !== undefined) {
            const parsed = {};
            for (let i = 0; i < 14; i++) parsed[`gate_${i}${ep}`] = data.moveEnergyThresholds.readUInt8(i);
            result[`move_energy_thresholds${ep}`] = parsed;
        }

        if (data.energyStatStill !== undefined) {
            const parsed = {};
            for (let i = 0; i < 14; i++) {
                const offset = i * 3;
                parsed[`gate_${i}${ep}`] = `min: ${data.energyStatStill.readUInt8(offset)}, max: ${data.energyStatStill.readUInt8(offset + 1)}, avg: ${data.energyStatStill.readUInt8(offset + 2)}`;
            }
            result[`energy_stat_still${ep}`] = parsed;
        }

        if (data.energyStatMove !== undefined) {
            const parsed = {};
            for (let i = 0; i < 14; i++) {
                const offset = i * 3;
                parsed[`gate_${i}${ep}`] = `min: ${data.energyStatMove.readUInt8(offset)}, max: ${data.energyStatMove.readUInt8(offset + 1)}, avg: ${data.energyStatMove.readUInt8(offset + 2)}`;
            }
            result[`energy_stat_move${ep}`] = parsed;
        }

        if (data.lightSense !== undefined) {
            const modeRaw = data.lightSense.readUInt8(0);
            const modeMap = { 0: 'Off', 1: 'DetectWhenLessThan', 2: 'DetectWhenBiggerThan' };
            result[`light_sense${ep}`] = {
                [`mode${ep}`]: modeMap[modeRaw] || 'Off',
                [`threshold${ep}`]: data.lightSense.readUInt8(1)
            };
        }

        return result;
    },
};

const tzLocal = {
    key: [
        'base_config', 'still_energy_thresholds', 'move_energy_thresholds', 'light_sense',
        'statistics_sample_count_window', 'bluetooth_state', 'exec_cmd',
        'energy_stat_still', 'energy_stat_move'
    ],
    convertSet: async (entity, key, value, meta) => {
        const ep = meta.endpoint_name ? `_${meta.endpoint_name}` : ''
        const stateKey = `${key}${ep}`
        const state = meta.state[stateKey] || {};
        let payload = {};

        let targetEntity = entity;
        if (meta.endpoint_name && meta.mapped && meta.mapped.endpoint) {
            const epMap = meta.mapped.endpoint(meta.device);
            if (epMap[meta.endpoint_name]) {
                targetEntity = meta.device.getEndpoint(epMap[meta.endpoint_name]) || entity;
            }
        }

        if (key === 'base_config') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(11);
            buf.writeFloatLE(merged[`range_min${ep}`] !== undefined ? merged[`range_min${ep}`] : 0.0, 0);
            buf.writeFloatLE(merged[`range_max${ep}`] !== undefined ? merged[`range_max${ep}`] : 8.0, 4);
            buf.writeUInt16LE(merged[`clear_delay${ep}`] !== undefined ? merged[`clear_delay${ep}`] : 5, 8);
            
            let res = 0;
            if (merged[`distance_resolution${ep}`] === '0.50') res = 1;
            else if (merged[`distance_resolution${ep}`] === '0.20') res = 3;
            buf.writeUInt8(res, 10);
            payload = { baseConfig: buf };
            
            await targetEntity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        } 
        
        if (key === 'still_energy_thresholds' || key === 'move_energy_thresholds') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(14);
            for (let i = 0; i < 14; i++) {
                buf.writeUInt8(merged[`gate_${i}${ep}`] !== undefined ? merged[`gate_${i}${ep}`] : 50, i);
            }
            payload = key === 'still_energy_thresholds' ? { stillEnergyThresholds: buf } : { moveEnergyThresholds: buf };
            
            await targetEntity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        }

        if (key === 'light_sense') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(2);
            let mode = 0;
            if (merged[`mode${ep}`] === 'DetectWhenLessThan') mode = 1;
            else if (merged[`mode${ep}`] === 'DetectWhenBiggerThan') mode = 2;
            buf.writeUInt8(mode, 0);
            buf.writeUInt8(merged[`threshold${ep}`] !== undefined ? merged[`threshold${ep}`] : 128, 1);
            payload = { lightSense: buf };
            
            await targetEntity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        }

        if (key === 'statistics_sample_count_window') {
            await targetEntity.write('hlkLD2412', { statSampleWindow: value }, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: value } };
        }

        if (key === 'bluetooth_state') {
            await targetEntity.write('hlkLD2412', { bluetoothState: value ? 1 : 0 }, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: value } };
        }

        if (key === 'exec_cmd') {
            const cmdMap = {
                'restart': 'restart',
                'factory_reset': 'factoryReset',
                'run_back_analysis': 'runBackAnalysis',
                'take_stat_snapshot': 'takeStatSnapshot'
            };
            if (cmdMap[value]) {
                await targetEntity.command('hlkLD2412', cmdMap[value], {}, { customCluster: hlkLD2412Cluster });
            }
            return; 
        }
    },
    // Read requests
    convertGet: async (entity, key, meta) => {
        const keyToAttr = {
            'base_config': 'baseConfig',
            'sw_ver': 'swVer',
            'bluetooth_mac': 'bluetoothMac',
            'still_energy_thresholds': 'stillEnergyThresholds',
            'move_energy_thresholds': 'moveEnergyThresholds',
            'light_level': 'lightLevel',
            'background_analysis_active': 'flags',
            'background_analysis_ok': 'flags',
            'statistics_sample_count_window': 'statSampleWindow',
            'light_sense': 'lightSense',
            'bluetooth_state': 'bluetoothState',
            'energy_stat_still': 'energyStatStill',
            'energy_stat_move': 'energyStatMove'   
        };

        const attr = keyToAttr[key];
        if (attr) {
            await entity.read('hlkLD2412', [attr], { customCluster: hlkLD2412Cluster });
        }
    }
};

// 3. ModernExtend Factory Function
function hlkLd2412(ep, epId) {
    const createThresholdExpose = (name, desc) => {
        const comp = e.composite(name, name, ea.ALL).withDescription(desc).withEndpoint(ep);
        for (let i = 0; i < 14; i++) {
            comp.withFeature(e.numeric(`gate_${i}`, ea.ALL).withValueMin(0).withValueMax(100));
        }
        return comp;
    };

    const createStatExpose = (name, desc) => {
        // Use ea.STATE_GET so the UI generates a read/refresh button
        const comp = e.composite(name, name, ea.STATE_GET).withDescription(desc).withEndpoint(ep);
        for (let i = 0; i < 14; i++) {
            comp.withFeature(e.text(`gate_${i}`, ea.STATE_GET).withDescription(`Gate ${i} (min, max, avg)`));
        }
        return comp;
    };

    const exposesList = [
        e.composite('base_config', 'base_config', ea.ALL).withDescription('Base Configuration').withEndpoint(ep)
            .withFeature(e.numeric('range_min', ea.ALL).withUnit('m'))
            .withFeature(e.numeric('range_max', ea.ALL).withUnit('m'))
            .withFeature(e.numeric('clear_delay', ea.ALL).withUnit('s'))
            .withFeature(e.enum('distance_resolution', ea.ALL, ['0.75', '0.50', '0.20'])),
        
        createThresholdExpose('still_energy_thresholds', 'Still Energy Gates'),
        createThresholdExpose('move_energy_thresholds', 'Move Energy Gates'),

        e.composite('light_sense', 'light_sense', ea.ALL).withDescription('Light Sensitivity Config').withEndpoint(ep)
            .withFeature(e.enum('mode', ea.ALL, ['Off', 'DetectWhenLessThan', 'DetectWhenBiggerThan']))
            .withFeature(e.numeric('threshold', ea.ALL).withValueMin(0).withValueMax(255)),

        e.numeric('light_level', ea.STATE).withDescription('Current Light Level').withEndpoint(ep),
        e.numeric('statistics_sample_count_window', ea.ALL).withValueMin(0).withValueMax(128).withEndpoint(ep),
        e.enum('exec_cmd', ea.SET, ['restart', 'factory_reset', 'run_back_analysis', 'take_stat_snapshot']).withEndpoint(ep),
        createStatExpose('energy_stat_still', 'Still Energy Stats'),
        createStatExpose('energy_stat_move', 'Move Energy Stats'),

        e.binary('background_analysis_active', ea.STATE, true, false).withEndpoint(ep),
        e.binary('background_analysis_ok', ea.STATE, true, false).withEndpoint(ep),
        e.binary('bluetooth_state', ea.ALL, true, false).withDescription('Bluetooth State').withEndpoint(ep),
        e.text('bluetooth_mac', ea.STATE).withDescription('Bluetooth MAC').withEndpoint(ep),
        e.text('sw_ver', ea.STATE).withDescription('Firmware Version').withEndpoint(ep),
    ];

    const configureLocal = async (device, coordinatorEndpoint, logger) => {
        const endpoint = device.getEndpoint(epId);
        
        if (!endpoint) {
            logger.error(`[hlkLD2412] Endpoint ${epId} (${ep}) not found on device.`);
            return;
        }

        await endpoint.bind('hlkLD2412', coordinatorEndpoint, { customCluster: hlkLD2412Cluster });

        await endpoint.configureReporting('hlkLD2412', [
            {
                attribute: 'lightLevel',
                minimumReportInterval: 5,
                maximumReportInterval: 3600,
                reportableChange: 5,
            },
            {
                attribute: 'flags',
                minimumReportInterval: 1,
                maximumReportInterval: 3600,
                reportableChange: 1, 
            }
        ], { customCluster: hlkLD2412Cluster });

        // 3. NEW: Read initial states (chunked to prevent oversized Zigbee packets)
        try {
            // Chunk 1: Basic Config & System Info
            await endpoint.read('hlkLD2412', ['baseConfig', 'swVer', 'bluetoothMac', 'bluetoothState'], { customCluster: hlkLD2412Cluster });
            
            // Chunk 2: Threshold Gates (14 bytes each = 28 bytes total response payload)
            await endpoint.read('hlkLD2412', ['stillEnergyThresholds', 'moveEnergyThresholds'], { customCluster: hlkLD2412Cluster });
            
            // Chunk 3: Small misc settings
            await endpoint.read('hlkLD2412', ['lightSense', 'lightLevel', 'flags', 'statSampleWindow'], { customCluster: hlkLD2412Cluster });
            
            // (Optional) Chunk 4: If you want the massive 42-byte stats read immediately too:
            // await endpoint.read('hlkLD2412', ['energyStatStill'], { customCluster: hlkLD2412Cluster });
            // await endpoint.read('hlkLD2412', ['energyStatMove'], { customCluster: hlkLD2412Cluster });
            
        } catch (error) {
            // If the device is asleep or drops a packet, we log it but don't crash the whole config process
            logger.error(`[hlkLD2412] Failed to read initial states for endpoint ${endpoint.ID}: ${error.message}`);
        }
    };

    return {
        isModernExtend: true,
        fromZigbee: [fzLocal],
        toZigbee: [tzLocal],
        exposes: exposesList,
        configure: [configureLocal], 
    };
}

const orlangurLD2412Extended = {
    ens160AirQ: () => {
        const aqi_lookup = {
            "Excellent"  : 1,
            "Good"       : 2,
            "Moderate"   : 3,
            "Poor"       : 4,
            "Unhealthy"  : 5,
        };
        const exposes = [
            e.numeric('voc', ea.STATE_GET).withLabel('TVOC').withCategory('diagnostic'),
            e.enum('aqi', ea.STATE_GET, Object.keys(aqi_lookup)).withLabel('AQI').withCategory('diagnostic'),
        ];

        const fromZigbee = [
            {
                cluster: 'ens160airQuality',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    const result = {};
                    const data = msg.data;
                    if (data['tvoc'] !== undefined) 
                        result['voc'] = data['tvoc'];
                    if (data['aqi'] !== undefined) 
                    {
                        const v = data['aqi']
                        const entry = Object.entries(aqi_lookup).find(([_,val])=> val == v)
                        if (entry)
                            result['aqi'] = entry[0];//key
                    }
                    return result
                }
            }
        ];

        const toZigbee = [
            {
                key: ['voc', 'aqi'],
                convertGet: async (entity, key, meta) => {
                    await entity.read('ens160airQuality', [key]);
                },
            }
        ];

        return {
            exposes,
            fromZigbee,
            toZigbee,
            isModernExtend: true,
        };
    },
    extendedStatus: () => {
        const exposes = [
            e.enum('status1_error', ea.STATE_GET, Object.values(LD2412_ERR))
                .withLabel('Status1 Error').withCategory('diagnostic'),
            e.numeric('status1_raw', ea.STATE_GET).withLabel('Status1 Raw').withCategory('diagnostic'),

            e.binary('status2_pir', ea.STATE_GET, 1, 0).withLabel('PIR Presence').withCategory('diagnostic'),
            e.binary('status2_main', ea.STATE_GET, 1, 0).withLabel('Main Presence').withCategory('diagnostic'),
            e.binary('status2_aux', ea.STATE_GET, 1, 0).withLabel('Aux Presence').withCategory('diagnostic'),
            e.binary('status2_pir_changed', ea.STATE_GET, 1, 0).withLabel('PIR Changed').withCategory('diagnostic'),
            e.binary('status2_main_changed', ea.STATE_GET, 1, 0).withLabel('Main Changed').withCategory('diagnostic'),
            e.binary('status2_aux_changed', ea.STATE_GET, 1, 0).withLabel('Aux Changed').withCategory('diagnostic'),
            e.numeric('status2_raw', ea.STATE_GET).withLabel('Status2 Raw').withCategory('diagnostic'),

            e.text('status3_coredump_exists', ea.STATE_GET).withLabel('Coredump Exists').withCategory('diagnostic'),
            e.binary('status3_watchdog_config_error', ea.STATE_GET, 1, 0).withLabel('Watchdog Config Error').withCategory('diagnostic'),
            e.text('status3_last_reset_reason', ea.STATE_GET).withLabel('Last Reset').withCategory('diagnostic'),
            e.binary('status3_analysis_for_presence', ea.STATE_GET, 1, 0).withLabel('Analysis For Presence').withCategory('diagnostic'),
            e.binary('status3_analysis_for_absence', ea.STATE_GET, 1, 0).withLabel('Analysis For Absence').withCategory('diagnostic'),
            e.binary('status3_tx_error', ea.STATE_GET, 1, 0).withLabel('TX Error').withCategory('diagnostic'),
            e.numeric('status3_raw', ea.STATE_GET).withLabel('Status3 Raw').withCategory('diagnostic'),

            e.enum('stop_watchdog_feeding', ea.SET, ['trigger'])
                .withDescription('Stop feeding watchdog'),
            e.enum('clear_coredump', ea.SET, ['trigger'])
                .withDescription('Clear Coredump'),
        ];

        const fromZigbee = [
            {
                cluster: 'customStatus',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    const result = {};
                    const data = msg.data;

                    if (data['status1'] !== undefined) {
                        const raw = data['status1'];
                        result['status1_err'] = LD2412_ERR[raw] || `Unknown(${raw})`;
                        result['status1_error'] = LD2412_ERR[raw] || `Unknown(${raw})`;
                        result['status1_raw'] = raw;
                    }

                    if (data['status2'] !== undefined) {
                        const raw = data['status2'];
                        const lower = raw & 0x07;
                        const upper = (raw >> 8) & 0x07;
                        result['status2_pir'] = (lower >> 0) & 1;
                        result['status2_main'] = (lower >> 1) & 1;
                        result['status2_aux'] = (lower >> 2) & 1;
                        result['status2_pir_changed'] = (upper >> 0) & 1;
                        result['status2_main_changed'] = (upper >> 1) & 1;
                        result['status2_aux_changed'] = (upper >> 2) & 1;
                        result['status2_raw'] = raw;
                    }

                    if (data['status3'] !== undefined) {
                        const raw = data['status3'];
                        result['status3_watchdog_config_error'] = (raw >> 1) & 1;
                        result['status3_analysis_for_presence'] = (raw >> 2) & 1;
                        result['status3_analysis_for_absence'] = (raw >> 3) & 1;
                        result['status3_tx_error'] = (raw >> 11) & 1;
                        result['status3_raw'] = raw;
                        var reset_reason = '';
                        if ((raw >> 4) & 1) reset_reason += "pin;";
                        if ((raw >> 5) & 1) reset_reason += "wdt;";
                        if ((raw >> 6) & 1) reset_reason += "sw;";
                        if ((raw >> 7) & 1) reset_reason += "cpu;";
                        if ((raw >> 8) & 1) reset_reason += "lp;";
                        if ((raw >> 9) & 1) reset_reason += "dbg;";
                        result['status3_last_reset_reason'] = reset_reason;

                        var coredump_state = '';
                        if ((raw >> 0) & 1) coredump_state += "core;";
                        if ((raw >> 10) & 1) coredump_state += "bread;";
                        result['status3_coredump_exists'] = coredump_state;
                    }

                    return result;
                },
            },
        ];

        const toZigbee = [
            {
                key: [
                    'status1_error', 'status1_raw',
                    'status2_pir', 'status2_main', 'status2_aux',
                    'status2_pir_changed', 'status2_main_changed', 'status2_aux_changed', 'status2_raw',
                    'status3_coredump_exists', 'status3_last_reset_reason', 'status3_watchdog_config_error', 'status3_analysis_for_presence', 
                    'status3_analysis_for_absence', 'status3_tx_error', 'status3_raw', 'stop_watchdog_feeding', 'clear_coredump'
                ],
                convertSet: async (entity, key, value, meta) => {
                    if (key === 'stop_watchdog_feeding' && value === 'trigger') {
                        await entity.command('customStatus', 'stop_watchdog_feeding', {}, { customCluster: customStatusCluster });
                    }
                    else if (key === 'clear_coredump' && value === 'trigger') {
                        await entity.command('customStatus', 'clear_coredump', {}, { customCluster: customStatusCluster });
                    }
                },
                convertGet: async (entity, key, meta) => {
                    await entity.read('customStatus', ['status1', 'status2', 'status3']);
                },
            },
        ];

        const configure = [];

        configure.push(
            setupConfigureForReading("customStatus", ["status1", "status2", "status3"]),
            setupConfigureForReporting("customStatus", "status1", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus", "status2", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus", "status3", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
        );

        return {
            exposes,
            fromZigbee,
            toZigbee,
            configure,
            isModernExtend: true,
        };
    },
    extendedStatus2: () => {
        const exposes = [
            e.text('status1_radio_error', ea.STATE_GET)
                .withLabel('Last Transmission Radio Error').withCategory('diagnostic'),
            e.numeric('status1_registered_fails', ea.STATE_GET).withLabel('Registered Radio Fails').withCategory('diagnostic'),
            e.numeric('s2_status1_raw', ea.STATE_GET).withLabel('S2 Status1 Raw').withCategory('diagnostic'),

            e.numeric('status2_trigger_to_send', ea.STATE_GET).withLabel('Trig-Send').withCategory('diagnostic'),
            e.numeric('status2_send_to_start', ea.STATE_GET).withLabel('Send-Start').withCategory('diagnostic'),
            e.numeric('status2_send_to_trans', ea.STATE_GET).withLabel('Send-Trans').withCategory('diagnostic'),
            e.numeric('status2_start_attempts', ea.STATE_GET).withLabel('Start Attempts').withCategory('diagnostic'),
            e.numeric('s2_status2_raw', ea.STATE_GET).withLabel('Status2 Raw').withCategory('diagnostic'),

            e.text('status3_pre_send', ea.STATE_GET).withLabel('Pre-Send Stat').withCategory('diagnostic'),
            e.numeric('s2_status3_raw', ea.STATE_GET).withLabel('Status3 Raw').withCategory('diagnostic'),
            e.text('status4_scheduler_delay', ea.STATE_GET).withLabel('Delays Stat').withCategory('diagnostic'),
            e.text('status4_tirg_to_zboss', ea.STATE_GET).withLabel('Trig-ZBoss').withCategory('diagnostic'),
            e.numeric('s2_status4_raw', ea.STATE_GET).withLabel('Status4 Raw').withCategory('diagnostic'),
        ];

        const fromZigbee = [
            {
                cluster: 'customStatus2',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    const result = {};
                    const data = msg.data;

                    if (data['status1'] !== undefined) {
                        const raw = data['status1'];
                        result['status1_registered_fails'] = (raw >> 20) & 0x0ff;

                        var radio_error = 'tx[';
                        if (raw & 0x000001) radio_error += 'busy;'
                        if (raw & 0x000002) radio_error += 'inv_ack;'
                        if (raw & 0x000004) radio_error += 'no_mem;'
                        if (raw & 0x000008) radio_error += 'ts_ended;'
                        if (raw & 0x000010) radio_error += 'no_ack;'
                        if (raw & 0x000020) radio_error += 'abort;'
                        if (raw & 0x000040) radio_error += 'ts_denied;'
                        if (raw & 0x000080) radio_error += 'key_id;'
                        if (raw & 0x000100) radio_error += 'fr_cnt;'
                        radio_error += '] rx['
                        if (raw & 0x000100) radio_error += 'i_fr;'
                        if (raw & 0x000200) radio_error += 'i_fcs;'
                        if (raw & 0x000400) radio_error += 'i_dst;'
                        if (raw & 0x000800) radio_error += 'rt;'
                        if (raw & 0x001000) radio_error += 'ts_end;'
                        if (raw & 0x002000) radio_error += 'abort;'
                        if (raw & 0x004000) radio_error += 'del_ts_denied;'
                        if (raw & 0x008000) radio_error += 'del_timeout;'
                        if (raw & 0x010000) radio_error += 'i_len;'
                        if (raw & 0x020000) radio_error += 'del_abort;'
                        if (raw & 0x040000) radio_error += 'no_buf;'
                        radio_error += ']'
                        result['status1_radio_error'] = radio_error;
                        result['s2_status1_raw'] = raw;
                    }

                    if (data['status2'] !== undefined) {
                        const raw = data['status2'];
                        const lower = raw & 0x07;
                        const upper = (raw >> 8) & 0x07;
                        result['status2_trigger_to_send'] = (raw & 0xff) * 10;
                        result['status2_send_to_start'] = ((raw >> 8) & 0xff) * 10;
                        result['status2_send_to_trans'] = ((raw >> 16) & 0xff) * 10;
                        result['status2_start_attempts'] = ((raw >> 24) & 0xff);
                        result['s2_status2_raw'] = raw;
                    }

                    if (data['status3'] !== undefined) {
                        const raw = data['status3'];
                        const stats = `tx_c:${(raw & 0xff)};tx_e:${((raw >> 8) & 0xff)};rx_c:${((raw >> 16) & 0xff)};rx_e:${((raw >> 24) & 0xff)};`;
                        result['status3_pre_send'] = stats;
                        result['s2_status3_raw'] = raw;
                    }

                    if (data['status4'] !== undefined) {
                        const raw = data['status4'];
                        const schedule_delay = raw & 0xffff;
                        const trigger_to_zboss = (raw >> 16) & 0xffff;
                        result['status4_scheduler_delay'] = schedule_delay;
                        result['status4_tirg_to_zboss'] = trigger_to_zboss;
                        result['s2_status4_raw'] = raw;
                    }

                    return result;
                },
            },
        ];

        const toZigbee = [
            {
                key: [
                    'status1_radio_error', 'status1_registered_fails',
                    's2_status1_raw', 'status2_trigger_to_send', 'status2_send_to_start',
                    'status2_send_to_trans', 'status2_start_attempts', 's2_status2_raw',
                    'status3_pre_send', 's2_status3_raw', 'status4_scheduler_delay',
                    'status4_tirg_to_zboss', 's2_status4_raw'
                ],
                convertSet: async (entity, key, value, meta) => {
                },
                convertGet: async (entity, key, meta) => {
                    await entity.read('customStatus2', ['status1', 'status2', 'status3', 'status4']);
                },
            },
        ];

        const configure = [];

        configure.push(
            setupConfigureForReading("customStatus2", ["status1", "status2"]),
            setupConfigureForReporting("customStatus2", "status1", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus2", "status2", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus2", "status3", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus2", "status4", {
                config: { min: "1_SECOND", max: "MAX", change: 1 },
                access: ea.STATE_GET,
            }),
        );

        return {
            exposes,
            fromZigbee,
            toZigbee,
            configure,
            isModernExtend: true,
        };
    },
    deviceControl: () => {
        const createStillGateEnergyExpose = (name, desc) => {
            // Use ea.STATE_GET so the UI generates a read/refresh button
            const comp = e.composite(name, name, ea.STATE_GET).withDescription(desc);
            for (let i = 0; i < 14; i++) {
                comp.withFeature(e.text(`gate_${i}`, ea.STATE_GET).withDescription(`Gate ${i} still energy`));
            }
            return comp;
        };
        const exposes = [
            e.enum('start_analysis_for_presence', ea.SET, ['trigger']).withDescription('Start Analysis for Presence'),
            e.enum('start_analysis_for_absense', ea.SET, ['trigger']).withDescription('Start Analysis for Absence'),
            e.enum('stop_analysis', ea.SET, ['trigger']).withDescription('Stop Analysis'),
            createStillGateEnergyExpose('main_still_energy_analysis', 'Main Still Energy Gates'),
            createStillGateEnergyExpose('aux_still_energy_analysis', 'Aux Still Energy Gates'),
        ];

        const fromZigbee = [
            {
                cluster: 'devCtrl',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    const result = {};
                    const data = msg.data;

                    if (data.main_still_energy_analysis !== undefined) {
                        const parsed = {};
                        for (let i = 0; i < 14; i++) parsed[`gate_${i}`] = data.main_still_energy_analysis.readUInt8(i);
                        result[`main_still_energy_analysis`] = parsed;
                    }

                    if (data.aux_still_energy_analysis !== undefined) {
                        const parsed = {};
                        for (let i = 0; i < 14; i++) parsed[`gate_${i}`] = data.aux_still_energy_analysis.readUInt8(i);
                        result[`aux_still_energy_analysis`] = parsed;
                    }
                    return result
                }
            }
        ];

        const toZigbee = [
            {
                key: [
                    'start_analysis_for_presence', 'start_analysis_for_absense',
                    'stop_analysis', 'main_still_energy_analysis', 'aux_still_energy_analysis'
                ],
                convertSet: async (entity, key, value, meta) => {
                    if ( value === 'trigger'
                          && 
                            (
                                key === 'start_analysis_for_presence'
                                || key === 'start_analysis_for_absense'
                                || key === 'stop_analysis'
                            )
                    ) {
                        await entity.command('devCtrl', key, {}, { customCluster: deviceControlCluster });
                    }
                },
                convertGet: async (entity, key, meta) => {
                    await entity.read('devCtrl', [key]);
                },
            },
        ];

        return {
            exposes,
            fromZigbee,
            toZigbee,
            isModernExtend: true
        }
    }
}

const definition = {
    zigbeeModel: ['LD2412-NG'],
    model: 'LD2412-NG',
    fingerprint: [{modelID: 'LD2412-NG', applicationVersion: 1, priority: -1},],
    vendor: 'SFINAE',
    description: 'LD2412-NG',
    extend: [
        deviceEndpoints({endpoints: {main: 1, aux: 2}}),
        deviceAddCustomCluster('customStatus', customStatusCluster),
        deviceAddCustomCluster('customStatus2', customStatus2Cluster),
        deviceAddCustomCluster('ens160airQuality', {
            name: "ens160airQuality",
            ID: 0xfc08,
            attributes: {
                tvoc: { name: "tvoc", ID: 0x0000, type: Zcl.DataType.SINGLE_PREC},
                aqi:  { name: "aqi", ID: 0x0001, type: Zcl.DataType.ENUM8},
            },
            commands: {},
            commandsResponse: {}
        }),
        deviceAddCustomCluster('hlkLD2412', hlkLD2412Cluster),
        deviceAddCustomCluster('devCtrl', deviceControlCluster),
        orlangurLD2412Extended.extendedStatus(),
        orlangurLD2412Extended.extendedStatus2(),
        occupancy({ultrasonicConfig:["otu_delay"], endpointNames: ["main"]}),
        co2(),
        temperature(),
        humidity(),
        enumLookup({
            cluster: "ens160airQuality",
            attribute: "aqi",
            name: "aqi",
            lookup: {
                "Excellent"  : 1,
                "Good"       : 2,
                "Moderate"   : 3,
                "Poor"       : 4,
                "Unhealthy"  : 5,
            },
            access: "STATE_GET",
            reporting: {min: 5, max: 120, change: 1},
            entityCategory: "diagnostic",
            label: "AQI"
        }),
        numeric({
            cluster: "ens160airQuality",
            attribute: "tvoc",
            name: "voc",
            access: "STATE_GET",
            reporting: {min: 5, max: 120, change: 1},
            entityCategory: "diagnostic",
            label: "TVOC",
            precision: 0
        }),
        hlkLd2412("main", 1)
        ,hlkLd2412("aux", 2)
        ,orlangurLD2412Extended.deviceControl()
    ]
};

module.exports = definition;
