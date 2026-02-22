const { Zcl } = require('zigbee-herdsman');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;
const ea = exposes.access;

// 1. Cluster Definition using named Zcl constants
export const hlkLD2412Cluster = {
    ID: 0xFC82,
    manufacturerCode: null,
    attributes: {
        baseConfig: { ID: 0x0000, type: Zcl.DataType.OCTET_STR },
        swVer: { ID: 0x0001, type: Zcl.DataType.CHAR_STR },
        bluetoothMac: { ID: 0x0002, type: Zcl.DataType.OCTET_STR },
        stillEnergyThresholds: { ID: 0x0003, type: Zcl.DataType.OCTET_STR },
        moveEnergyThresholds: { ID: 0x0004, type: Zcl.DataType.OCTET_STR },
        lightLevel: { ID: 0x0005, type: Zcl.DataType.UINT8 },
        flags: { ID: 0x0006, type: Zcl.DataType.UINT8 },
        statSampleWindow: { ID: 0x0007, type: Zcl.DataType.UINT8 },
        energyStatStill: { ID: 0x0008, type: Zcl.DataType.OCTET_STR },
        energyStatMove: { ID: 0x0009, type: Zcl.DataType.OCTET_STR },
        lightSense: { ID: 0x000a, type: Zcl.DataType.OCTET_STR },
        bluetoothState: { ID: 0x000b, type: Zcl.DataType.BOOLEAN },
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
        const ep = meta.endpoint_name ? `_${meta.endpoint_name}` : ''; 
        const data = msg.data;

        if (data.baseConfig !== undefined) {
            const buf = data.baseConfig;
            const distResRaw = buf.readUInt8(10);
            result[`base_config${ep}`] = {
                range_min: buf.readFloatLE(0),
                range_max: buf.readFloatLE(4),
                clear_delay: buf.readUInt16LE(8),
                distance_resolution: distResRaw === 3 ? '0.20' : (distResRaw === 1 ? '0.50' : '0.75'),
            };
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
            for (let i = 0; i < 14; i++) parsed[`gate_${i}`] = data.stillEnergyThresholds.readUInt8(i);
            result[`still_energy_thresholds${ep}`] = parsed;
        }

        if (data.moveEnergyThresholds !== undefined) {
            const parsed = {};
            for (let i = 0; i < 14; i++) parsed[`gate_${i}`] = data.moveEnergyThresholds.readUInt8(i);
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
                mode: modeMap[modeRaw] || 'Off',
                threshold: data.lightSense.readUInt8(1)
            };
        }

        return result;
    },
};

const tzLocal = {
    key: [
        'base_config', 'still_energy_thresholds', 'move_energy_thresholds', 'light_sense',
        'statistics_sample_count_window', 'bluetooth_state', 'action'
    ],
    convertSet: async (entity, key, value, meta) => {
        const stateKey = meta.endpoint_name ? `${key}_${meta.endpoint_name}` : key;
        const state = meta.state[stateKey] || {};
        let payload = {};

        if (key === 'base_config') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(11);
            buf.writeFloatLE(merged.range_min !== undefined ? merged.range_min : 0.0, 0);
            buf.writeFloatLE(merged.range_max !== undefined ? merged.range_max : 8.0, 4);
            buf.writeUInt16LE(merged.clear_delay !== undefined ? merged.clear_delay : 5, 8);
            
            let res = 0;
            if (merged.distance_resolution === '0.50') res = 1;
            else if (merged.distance_resolution === '0.20') res = 3;
            buf.writeUInt8(res, 10);
            payload = { baseConfig: buf };
            
            await entity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        } 
        
        if (key === 'still_energy_thresholds' || key === 'move_energy_thresholds') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(14);
            for (let i = 0; i < 14; i++) {
                buf.writeUInt8(merged[`gate_${i}`] !== undefined ? merged[`gate_${i}`] : 50, i);
            }
            payload = key === 'still_energy_thresholds' ? { stillEnergyThresholds: buf } : { moveEnergyThresholds: buf };
            
            await entity.write('hlkLD2412', payload, { customCluster: hlkLD2412Cluster });
            return { state: { [stateKey]: merged } };
        }

        if (key === 'light_sense') {
            const merged = { ...state, ...value };
            const buf = Buffer.alloc(2);
            let mode = 0;
            if (merged.mode === 'DetectWhenLessThan') mode = 1;
            else if (merged.mode === 'DetectWhenBiggerThan') mode = 2;
            buf.writeUInt8(mode, 0);
            buf.writeUInt8(merged.threshold !== undefined ? merged.threshold : 128, 1);
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

        if (key === 'action') {
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
};

// 3. ModernExtend Factory Function
function hlkLd2412() {
    const createThresholdExpose = (name, desc) => {
        const comp = e.composite(name, name, ea.ALL).withDescription(desc);
        for (let i = 0; i < 14; i++) {
            comp.withFeature(e.numeric(`gate_${i}`, ea.ALL).withValueMin(0).withValueMax(100));
        }
        return comp;
    };

    const statExposes = [];
    for (let i = 0; i < 14; i++) {
        statExposes.push(e.text(`stat_still_gate_${i}`, ea.STATE).withDescription(`Still Stats Gate ${i}`));
        statExposes.push(e.text(`stat_move_gate_${i}`, ea.STATE).withDescription(`Move Stats Gate ${i}`));
    }

    const exposesList = [
        e.composite('base_config', 'base_config', ea.ALL).withDescription('Base Configuration')
            .withFeature(e.numeric('range_min', ea.ALL).withUnit('m'))
            .withFeature(e.numeric('range_max', ea.ALL).withUnit('m'))
            .withFeature(e.numeric('clear_delay', ea.ALL).withUnit('s'))
            .withFeature(e.enum('distance_resolution', ea.ALL, ['0.75', '0.50', '0.20'])),
        
        createThresholdExpose('still_energy_thresholds', 'Still Energy Gates'),
        createThresholdExpose('move_energy_thresholds', 'Move Energy Gates'),

        e.composite('light_sense', 'light_sense', ea.ALL).withDescription('Light Sensitivity Config')
            .withFeature(e.enum('mode', ea.ALL, ['Off', 'DetectWhenLessThan', 'DetectWhenBiggerThan']))
            .withFeature(e.numeric('threshold', ea.ALL).withValueMin(0).withValueMax(255)),

        e.numeric('light_level', ea.STATE).withDescription('Current Light Level'),
        e.numeric('statistics_sample_count_window', ea.ALL).withValueMin(0).withValueMax(128),
        e.binary('background_analysis_active', ea.STATE, true, false),
        e.binary('background_analysis_ok', ea.STATE, true, false),
        e.binary('bluetooth_state', ea.ALL, true, false).withDescription('Bluetooth State'),
        e.text('bluetooth_mac', ea.STATE).withDescription('Bluetooth MAC'),
        e.text('sw_ver', ea.STATE).withDescription('Firmware Version'),
        
        e.enum('action', ea.SET, ['restart', 'factory_reset', 'run_back_analysis', 'take_stat_snapshot']),
        
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

// 4. Device Definition
const definition = {
    zigbeeModel: ['Custom_LD2412'], 
    model: 'LD2412_Custom_Sensor',
    vendor: 'CustomMaker',
    description: 'Custom HighLink LD2412 mmWave Sensor',
    extend: [
        hlkLd2412(), 
    ],
    meta: { multiEndpoint: true },
    endpoint: (device) => {
        return {
            sensor_1: 1, 
            sensor_2: 2, 
        };
    },
};

module.exports = definition;
