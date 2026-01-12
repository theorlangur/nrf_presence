const { Buffer } = require('node:buffer');
const util = require('node:util');
const {Zcl} = require('zigbee-herdsman');
const {enumLookup,numeric,deviceAddCustomCluster,onOff,binary,occupancy} = require('zigbee-herdsman-converters/lib/modernExtend');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const constants = require('zigbee-herdsman-converters/lib/constants');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const ota = require('zigbee-herdsman-converters/lib/ota');
const utils = require('zigbee-herdsman-converters/lib/utils');
const globalStore = require('zigbee-herdsman-converters/lib/store');
const {logger} = require('zigbee-herdsman-converters/lib/logger');
const e = exposes.presets;
const eo = exposes.options;
const ea = exposes.access;

const NS = 'zhc:orlangur';

const orlangurC4001Extended = {
    c4001Config: () => {
        const exposes = [
            e.numeric('range_min', ea.ALL)
                .withLabel('Range From')
                .withValueMin(0.6)
                .withValueMax(25)
                .withCategory('config'),
            e.numeric('range_max', ea.ALL)
                .withLabel('Range To')
                .withValueMin(0.6)
                .withValueMax(25)
                .withCategory('config'),
            e.numeric('detect_delay', ea.ALL)
                .withLabel('Detect Delay')
                .withValueMin(0)
                .withValueStep(0.1)
                .withValueMax(2)
                .withCategory('config'),
            e.numeric('clear_delay', ea.ALL)
                .withLabel('Clear Delay')
                .withValueMin(0)
                .withValueMax(20)
                .withCategory('config'),
            e.numeric('range_trig', ea.ALL)
                .withLabel('Trigger Distance')
                .withValueMin(0.6)
                .withValueMax(25)
                .withCategory('config'),
            e.numeric('inhibit_duration', ea.ALL)
                .withLabel('Inhibit Duration')
                .withCategory('config'),
            e.numeric('sensitivity_detect', ea.ALL)
                .withLabel('Sensitivity Detect')
                .withValueMin(1)
                .withValueMax(9)
                .withValueStep(1)
                .withCategory('config'),
            e.numeric('sensitivity_hold', ea.ALL)
                .withLabel('Sensitivity Hold')
                .withValueMin(1)
                .withValueMax(9)
                .withValueStep(1)
                .withCategory('config'),
            e.numeric('sw_ver', ea.STATE_GET)
                .withLabel('Software Version')
                .withCategory('diagnostic'),
            e.numeric('hw_ver', ea.STATE_GET)
                .withLabel('Hardware Version')
                .withCategory('diagnostic'),
            e.enum("cmd_restart", ea.SET, ["Restart"])
                .withDescription("Restart C4001")
                .withCategory("config"),
            e.enum("cmd_save_config", ea.SET, ["Save Config"])
                .withDescription("Save Configuration on C4001")
                .withCategory("config"),
            e.enum("cmd_reset_config", ea.SET, ["Reset Config"])
                .withDescription("Reset Configuration on C4001")
                .withCategory("config"),
        ];
        const attributes = ['range_min', 'range_max', 'range_trig', 'inhibit_duration', 'sensitivity_detect', 'sensitivity_hold', 'sw_ver', 'hw_ver', 'detect_delay', 'clear_delay'];
        const fromZigbee = [
            {
                cluster: 'c40001Config',
                type: ['attributeReport', 'readResponse'],
                convert: (model, msg, publish, options, meta) => {
                    const result = {};
                    const data = msg.data;
                    for (const attr of attributes) {
                        if (data[attr] !== undefined) 
                            result[attr] = data[attr];
                    }
                    return result;
                }
            }
        ];

        const toZigbee = [
            {
                key: attributes,
                convertGet: async (entity, key, meta) => {
                    await entity.read('c40001Config', [key]);
                },
                convertSet: async (entity, key, value, meta) => {
                    await entity.write('c40001Config', {[key]: value});
                    return {state: {[key]: value}};
                },
            },
            {
                key: ["cmd_restart"],
                convertSet: async (entity, key, value, meta) => {
                    await entity.command("c40001Config", "restartC4001", {}, { disableDefaultResponse: true, });
                },
            }
            ,{
                key: ["cmd_save_config"],
                convertSet: async (entity, key, value, meta) => {
                    await entity.command("c40001Config", "saveConfigC4001", {}, { disableDefaultResponse: true, });
                },
            }
            ,{
                key: ["cmd_reset_config"],
                convertSet: async (entity, key, value, meta) => {
                    await entity.command("c40001Config", "resetConfigC4001", {}, { disableDefaultResponse: true, });
                },
            }
        ];

        const configure = [];

        configure.push(
            setupConfigureForReading("customStatus", ['range_min', 'range_max', 'range_trig', 'sw_ver', 'hw_ver', 'inhibit_duration'
            , 'sensitivity_detect' 
            , 'sensitivity_hold' 
            , 'detect_delay' 
            , 'clear_delay' ]),
        );

        return {
            exposes,
            fromZigbee,
            toZigbee,
            configure,
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
    zigbeeModel: ['C4001-NG'],
    model: 'C4001-NG',
    fingerprint: [{modelID: 'C4001-NG', applicationVersion: 1, priority: -1},],
    vendor: 'SFINAE',
    description: 'C4001-NG',
    extend: [
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
        deviceAddCustomCluster('c40001Config', {
            ID: 0xfc81,
            attributes: {
                range_min:            {ID: 0x0000, type: Zcl.DataType.SINGLE_PREC},
                range_max:            {ID: 0x0001, type: Zcl.DataType.SINGLE_PREC},
                range_trig:           {ID: 0x0002, type: Zcl.DataType.SINGLE_PREC},
                inhibit_duration:     {ID: 0x0003, type: Zcl.DataType.SINGLE_PREC},
                sensitivity_detect:   {ID: 0x0004, type: Zcl.DataType.UINT8},
                sensitivity_hold:     {ID: 0x0005, type: Zcl.DataType.UINT8},

                sw_ver:               {ID: 0x0006, type: Zcl.DataType.CHAR_STR},
                hw_ver:               {ID: 0x0007, type: Zcl.DataType.CHAR_STR},

                detect_delay:         {ID: 0x0008, type: Zcl.DataType.SINGLE_PREC},
                clear_delay:          {ID: 0x0009, type: Zcl.DataType.SINGLE_PREC},
            },
            commands: {
                restartC4001:     { ID: 0x01, parameters: [], },
                saveConfigC4001:  { ID: 0x02, parameters: [], },
                resetConfigC4001: { ID: 0x03, parameters: [], },
            },
            commandsResponse: {}
        }),
        orlangurC4001Extended.c4001Config(),
        orlangurC4001Extended.extendedStatus(),
        occupancy({ultrasonicConfig:["otu_delay", "uto_delay"]})
    ]
};

module.exports = definition;
