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

const orlangurC4001Extended = {
    c4001Cmds: (endpointName) => {
        const exposes = [
            e.enum("cmd_restart", ea.SET, ["Restart"])
                .withDescription("Restart C4001 ("+endpointName+")")
                .withEndpoint(endpointName)
                .withCategory("config"),
            e.enum("cmd_save_config", ea.SET, ["Save Config"])
                .withDescription("Save Configuration on C4001 ("+endpointName+")")
                .withEndpoint(endpointName)
                .withCategory("config"),
            e.enum("cmd_reset_config", ea.SET, ["Reset Config"])
                .withDescription("Reset Configuration on C4001 ("+endpointName+")")
                .withEndpoint(endpointName)
                .withCategory("config"),
        ];
        const toZigbee = [
            {
                key: ["cmd_restart"],
                convertSet: async (entity, key, value, meta) => {
                    await determineEndpoint(entity, meta, "c40001Config")
                        .command("c40001Config", "restartC4001", {}, { disableDefaultResponse: true, });
                },
            }
            ,{
                key: ["cmd_save_config"],
                convertSet: async (entity, key, value, meta) => {
                    await determineEndpoint(entity, meta, "c40001Config")
                        .command("c40001Config", "saveConfigC4001", {}, { disableDefaultResponse: true, });
                },
            }
            ,{
                key: ["cmd_reset_config"],
                convertSet: async (entity, key, value, meta) => {
                    await determineEndpoint(entity, meta, "c40001Config")
                        .command("c40001Config", "resetConfigC4001", {}, { disableDefaultResponse: true, });
                },
            }
        ];

        return {
            exposes,
            toZigbee,
            isModernExtend: true,
        };
    },
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
    zigbeeModel: ['C4001-NG'],
    model: 'C4001-NG',
    fingerprint: [{modelID: 'C4001-NG', applicationVersion: 1, priority: -1},],
    vendor: 'SFINAE',
    description: 'C4001-NG',
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
        deviceAddCustomCluster('ens160airQuality', {
            ID: 0xfc08,
            attributes: {
                tvoc: {ID: 0x0000, type: Zcl.DataType.SINGLE_PREC},
                aqi:  {ID: 0x0001, type: Zcl.DataType.ENUM8},
            },
            commands: {},
            commandsResponse: {}
        }),
        orlangurC4001Extended.extendedStatus(),
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
        numeric({
            cluster: "c40001Config",
            attribute: "range_min",
            name: "range_min",
            access: "ALL",
            entityCategory: "config",
            label: "Range From",
            unit: "m",
            valueMin: 0.6,
            valueMax: 25,
            precision: 1,
            endpointNames: ["main", "aux"]
        }),
        numeric({
            cluster: "c40001Config",
            attribute: "range_max",
            name: "range_max",
            access: "ALL",
            entityCategory: "config",
            label: "Range To",
            unit: "m",
            valueMin: 0.6,
            valueMax: 25,
            precision: 1,
            endpointNames: ["main", "aux"]
        }),
        numeric({
            cluster: "c40001Config",
            attribute: "range_trig",
            name: "range_trig",
            access: "ALL",
            entityCategory: "config",
            label: "Trigger Distance",
            unit: "m",
            valueMin: 0.6,
            valueMax: 25,
            precision: 1,
            endpointNames: ["main", "aux"]
        }),
        numeric({
            cluster: "c40001Config",
            attribute: "inhibit_duration",
            name: "inhibit_duration",
            access: "ALL",
            entityCategory: "config",
            label: "Inhibit Duration",
            unit: "s",
            endpointNames: ["main", "aux"],
        }),
        numeric({
            cluster: "c40001Config",
            attribute: "sensitivity_detect",
            name: "sensitivity_detect",
            access: "ALL",
            entityCategory: "config",
            label: "Sensitivity Detect",
            endpointNames: ["main", "aux"],
            valueMin: 1,
            valueMax: 9,
            valueStep: 1,
        }),
        numeric({
            cluster: "c40001Config",
            attribute: "sensitivity_hold",
            name: "sensitivity_hold",
            access: "ALL",
            entityCategory: "config",
            label: "Sensitivity Hold",
            endpointNames: ["main", "aux"],
            valueMin: 1,
            valueMax: 9,
            valueStep: 1,
        }),
        text({
            cluster: "c40001Config",
            attribute: "sw_ver",
            name: "sw_ver",
            access: "STATE_GET",
            label: "Software Version",
            entityCategory: "diagnostic",
            endpointName: "main",
        }),
        text({
            cluster: "c40001Config",
            attribute: "sw_ver",
            name: "sw_ver",
            access: "STATE_GET",
            label: "Software Version",
            entityCategory: "diagnostic",
            endpointName: "aux",
        }),
        text({
            cluster: "c40001Config",
            attribute: "hw_ver",
            name: "hw_ver",
            access: "STATE_GET",
            label: "Hardware Version",
            entityCategory: "diagnostic",
            endpointName: "main",
        }),
        text({
            cluster: "c40001Config",
            attribute: "hw_ver",
            name: "hw_ver",
            access: "STATE_GET",
            label: "Hardware Version",
            entityCategory: "diagnostic",
            endpointName: "aux",
        }),
        orlangurC4001Extended.c4001Cmds("main"),
        orlangurC4001Extended.c4001Cmds("aux"),
    ]
};

module.exports = definition;
