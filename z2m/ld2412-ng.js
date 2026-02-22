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

// 1. Cluster Definition using named Zcl constants
const hlkLD2412Cluster = {
    ID: 0xFC82,
    manufacturerCode: null,
    attributes: {
        baseConfig: { ID: 0x0000, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        swVer: { ID: 0x0001, type: Zcl.DataType.CHAR_STR, read: true, write: false },
        bluetoothMac: { ID: 0x0002, type: Zcl.DataType.OCTET_STR, read: true, write: false },
        stillEnergyThresholds: { ID: 0x0003, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        moveEnergyThresholds: { ID: 0x0004, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        lightLevel: { ID: 0x0005, type: Zcl.DataType.UINT8, read: true, write: false },
        flags: { ID: 0x0006, type: Zcl.DataType.UINT8, read: true, write: true },
        statSampleWindow: { ID: 0x0007, type: Zcl.DataType.UINT8, read: true, write: true },
        energyStatStill: { ID: 0x0008, type: Zcl.DataType.CHAR_STR, read: true, write: false },
        energyStatMove: { ID: 0x0009, type: Zcl.DataType.CHAR_STR, read: true, write: false },
        lightSense: { ID: 0x000a, type: Zcl.DataType.OCTET_STR, read: true, write: true },
        bluetoothState: { ID: 0x000b, type: Zcl.DataType.BOOLEAN, read: true, write: true },
    },
    commands: {
        restart: { ID: 0x0001, parameters: [] },
        factoryReset: { ID: 0x0002, parameters: [] },
        runBackAnalysis: { ID: 0x0003, parameters: [] },
        takeStatSnapshot: { ID: 0x0004, parameters: [] },
    },
    commandsResponse: {},
};

// 2. Converters
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
            for (let i = 0; i < 14; i++) {
                const offset = i * 3;
                result[`stat_still_gate_${i}${ep}`] = `min: ${data.energyStatStill.readUInt8(offset)}, max: ${data.energyStatStill.readUInt8(offset + 1)}, avg: ${data.energyStatStill.readUInt8(offset + 2)}`;
            }
        }

        if (data.energyStatMove !== undefined) {
            for (let i = 0; i < 14; i++) {
                const offset = i * 3;
                result[`stat_move_gate_${i}${ep}`] = `min: ${data.energyStatMove.readUInt8(offset)}, max: ${data.energyStatMove.readUInt8(offset + 1)}, avg: ${data.energyStatMove.readUInt8(offset + 2)}`;
            }
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
        'statistics_sample_count_window', 'bluetooth_state', 'exec_cmd'
    ],
    convertSet: async (entity, key, value, meta) => {
        const ep = meta.endpoint_name ? `_${meta.endpoint_name}` : ''
        const stateKey = `${key}${ep}`
        const state = meta.state[stateKey] || {};
        let payload = {};

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
            
            await entity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        } 
        
        if (key === 'still_energy_thresholds' || key === 'move_energy_thresholds') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(14);
            for (let i = 0; i < 14; i++) {
                buf.writeUInt8(merged[`gate_${i}${ep}`] !== undefined ? merged[`gate_${i}${ep}`] : 50, i);
            }
            payload = key === 'still_energy_thresholds' ? { stillEnergyThresholds: buf } : { moveEnergyThresholds: buf };
            
            await entity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
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
            
            await entity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        }

        if (key === 'statistics_sample_count_window') {
            await entity.write('hlkLD2412', { statSampleWindow: value }, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: value } };
        }

        if (key === 'bluetooth_state') {
            await entity.write('hlkLD2412', { bluetoothState: value ? 1 : 0 }, { customCluster: hlkLD2412Cluster });
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
                await entity.command('hlkLD2412', cmdMap[value], {}, { customCluster: hlkLD2412Cluster });
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
            'bluetooth_state': 'bluetoothState'
        };

        const attr = keyToAttr[key];
        if (attr) {
            await entity.read('hlkLD2412', [attr], { customCluster: hlkLD2412Cluster });
        }
    }
};

// 3. ModernExtend Factory Function
function hlkLd2412(ep) {
    const createThresholdExpose = (name, desc) => {
        const comp = e.composite(name, name, ea.ALL).withDescription(desc).withEndpoint(ep);
        for (let i = 0; i < 14; i++) {
            comp.withFeature(e.numeric(`gate_${i}`, ea.ALL).withValueMin(0).withValueMax(100));
        }
        return comp;
    };

    const statExposes = [];
    for (let i = 0; i < 14; i++) {
        statExposes.push(e.text(`stat_still_gate_${i}`, ea.STATE).withDescription(`Still Stats Gate ${i}`).withEndpoint(ep));
        statExposes.push(e.text(`stat_move_gate_${i}`, ea.STATE).withDescription(`Move Stats Gate ${i}`).withEndpoint(ep));
    }

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
        e.binary('background_analysis_active', ea.STATE, true, false).withEndpoint(ep),
        e.binary('background_analysis_ok', ea.STATE, true, false).withEndpoint(ep),
        e.binary('bluetooth_state', ea.ALL, true, false).withDescription('Bluetooth State').withEndpoint(ep),
        e.text('bluetooth_mac', ea.STATE).withDescription('Bluetooth MAC').withEndpoint(ep),
        e.text('sw_ver', ea.STATE).withDescription('Firmware Version').withEndpoint(ep),
        
        e.enum('exec_cmd', ea.SET, ['restart', 'factory_reset', 'run_back_analysis', 'take_stat_snapshot']).withEndpoint(ep),
        
        ...statExposes,
    ];

    const configureLocal = async (device, coordinatorEndpoint, logger) => {
        const validEndpoints = device.endpoints.filter((ep) => ep.ID !== 242);
        
        for (const endpoint of validEndpoints) {
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
                },
                {
                    attribute: 'energyStatStill',
                    minimumReportInterval: 1,
                    maximumReportInterval: 3600,
                    reportableChange: 0, 
                },
                {
                    attribute: 'energyStatMove',
                    minimumReportInterval: 1,
                    maximumReportInterval: 3600,
                    reportableChange: 0,
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
            e.numeric('tvoc', ea.STATE_GET).withLabel('TVOC').withCategory('diagnostic'),
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
                        result['tvoc'] = data['tvoc'];
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
                key: ['tvoc', 'aqi'],
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
            e.numeric('status1', ea.STATE_GET).withLabel('Status1').withCategory('diagnostic'),
            e.numeric('status2', ea.STATE_GET).withLabel('Status2').withCategory('diagnostic'),
            e.numeric('status3', ea.STATE_GET).withLabel('Status3').withCategory('diagnostic'),
        ];

        const fromZigbee = [
            {
                cluster: 'customStatus',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    const result = {};
                    const data = msg.data;
                    if (data['status1'] !== undefined) 
                        result['status1'] = data['status1'];
                    if (data['status2'] !== undefined) 
                        result['status2'] = data['status2'];
                    if (data['status3'] !== undefined) 
                        result['status3'] = data['status3'];
                    return result
                }
            }
        ];

        const toZigbee = [
            {
                key: ['status1', 'status2', 'status3'],
                convertGet: async (entity, key, meta) => {
                    await entity.read('customStatus', [key]);
                },
            }
        ];

        const configure = [];

        configure.push(
            setupConfigureForReading("customStatus", ["status1", "status2", "status3"]),
            setupConfigureForReporting("customStatus", "status1", {
                config: {min: "1_SECOND", max: "MAX", change: 1},
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus", "status2", {
                config: {min: "1_SECOND", max: "MAX", change: 1},
                access: ea.STATE_GET,
            }),
            setupConfigureForReporting("customStatus", "status3", {
                config: {min: "1_SECOND", max: "MAX", change: 1},
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
}

const definition = {
    zigbeeModel: ['LD2412-NG'],
    model: 'LD2412-NG',
    fingerprint: [{modelID: 'LD2412-NG', applicationVersion: 1, priority: -1},],
    vendor: 'SFINAE',
    description: 'LD2412-NG',
    extend: [
        deviceEndpoints({endpoints: {main: 1, aux: 2}}),
        deviceAddCustomCluster('customStatus', {
            ID: 0xfc80,
            attributes: {
                status1: {ID: 0x0000, type: Zcl.DataType.INT16},
                status2: {ID: 0x0001, type: Zcl.DataType.INT16},
                status3: {ID: 0x0002, type: Zcl.DataType.INT16},
            },
            commands: {},
            commandsResponse: {}
        }),
        deviceAddCustomCluster('ens160airQuality', {
            ID: 0xfc08,
            attributes: {
                tvoc: {ID: 0x0000, type: Zcl.DataType.SINGLE_PREC},
                aqi:  {ID: 0x0001, type: Zcl.DataType.ENUM8},
            },
            commands: {},
            commandsResponse: {}
        }),
        deviceAddCustomCluster('hlkLD2412', hlkLD2412Cluster),
        orlangurLD2412Extended.extendedStatus(),
        occupancy({ultrasonicConfig:["otu_delay", "uto_delay"], endpointNames: ["main"]}),
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
            name: "tvoc",
            access: "STATE_GET",
            reporting: {min: 5, max: 120, change: 1},
            entityCategory: "diagnostic",
            label: "TVOC",
            precision: 0
        }),
        hlkLd2412("main")
        ,hlkLd2412("aux")
    ]
};

module.exports = definition;
