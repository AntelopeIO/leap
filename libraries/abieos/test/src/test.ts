// copyright defined in abieos/LICENSE.txt

'use strict';

const rpcEndpoint = 'http://localhost:8000';
const useRpcEndpoint = false;

const fetch = require('node-fetch');
const fastcall = require('fastcall');
const abiAbi = require('../../external/eosjs/src/abi.abi.json');
const transactionAbi = require('../../external/eosjs/src/transaction.abi.json');
import * as eosjs from '../../external/eosjs/src/eosjs-api';
import * as eosjs_jsonrpc from '../../external/eosjs/src/eosjs-jsonrpc';
import * as eosjs_jssig from '../../external/eosjs/src/eosjs-jssig';
import * as eosjs_ser from '../../external/eosjs/src/eosjs-serialize';

const useTokenHexApi = true;
const tokenHexApi =
    '0e656f73696f3a3a6162692f312e30010c6163636f756e745f6e616d65046e61' +
    '6d6505087472616e7366657200040466726f6d0c6163636f756e745f6e616d65' +
    '02746f0c6163636f756e745f6e616d65087175616e7469747905617373657404' +
    '6d656d6f06737472696e67066372656174650002066973737565720c6163636f' +
    '756e745f6e616d650e6d6178696d756d5f737570706c79056173736574056973' +
    '737565000302746f0c6163636f756e745f6e616d65087175616e746974790561' +
    '73736574046d656d6f06737472696e67076163636f756e7400010762616c616e' +
    '63650561737365740e63757272656e63795f7374617473000306737570706c79' +
    '0561737365740a6d61785f737570706c79056173736574066973737565720c61' +
    '63636f756e745f6e616d6503000000572d3ccdcd087472616e73666572000000' +
    '000000a531760569737375650000000000a86cd4450663726561746500020000' +
    '00384f4d113203693634010863757272656e6379010675696e74363407616363' +
    '6f756e740000000000904dc603693634010863757272656e6379010675696e74' +
    '36340e63757272656e63795f7374617473000000';

const testAbi = `{
        "version": "eosio::abi/1.0",
        "structs": [
            {
                "name": "s1",
                "base": "",
                "fields": [
                    {
                        "name": "x1",
                        "type": "int8"
                    }
                ]
            },
            {
                "name": "s2",
                "base": "",
                "fields": [
                    {
                        "name": "y1",
                        "type": "int8$"
                    },
                    {
                        "name": "y2",
                        "type": "int8$"
                    }
                ]
            },
            {
                "name": "s3",
                "base": "",
                "fields": [
                    {
                        "name": "z1",
                        "type": "int8$"
                    },
                    {
                        "name": "z2",
                        "type": "v1$"
                    },
                    {
                        "name": "z3",
                        "type": "s2$"
                    }
                ]
            },
            {
                "name": "s4",
                "base": "",
                "fields": [
                    {
                        "name": "a1",
                        "type": "int8?$"
                    },
                    {
                        "name": "b1",
                        "type": "int8[]$"
                    }
                ]
            }
        ],
        "variants": [
            {
                "name": "v1",
                "types": ["int8","s1","s2"]
            }
        ]
    }`;

const lib = new fastcall.Library('../build/libabieos.so')
    .function('void* abieos_create()')
    .function('void abieos_destroy(void* context)')
    .function('char* abieos_get_error(void* context)')
    .function('int abieos_get_bin_size(void* context)')
    .function('char* abieos_get_bin_data(void* context)')
    .function('char* abieos_get_bin_hex(void* context)')
    .function('uint64 abieos_string_to_name(void* context, char* str)')
    .function('char* abieos_name_to_string(void* context, uint64 name)')
    .function('int abieos_set_abi(void* context, uint64 contract, char* abi)')
    .function('int abieos_set_abi_hex(void* context, uint64 contract, char* hex)')
    .function('char* abieos_get_type_for_action(void* context, uint64 contract, uint64 action)')
    .function('int abieos_json_to_bin(void* context, uint64 contract, char* name, char* json)')
    .function('char* abieos_hex_to_json(void* context, uint64 contract, char* type, char* hex)');

const l = lib.interface;
const cstr = fastcall.makeStringBuffer;

const context = l.abieos_create();

function check(result: any) {
    if (!result)
        throw new Error(l.abieos_get_error(context).readCString());
}

function checkPtr(result: any) {
    if (result.isNull())
        throw new Error(l.abieos_get_error(context).readCString());
    return result;
}

function jsonStr(v: any) {
    return cstr(JSON.stringify(v));
}

function name(s: string) {
    return l.abieos_string_to_name(context, cstr(s));
}

const rpc = new eosjs_jsonrpc.JsonRpc(rpcEndpoint, { fetch });
const signatureProvider = new eosjs_jssig.JsSignatureProvider(['5JtUScZK2XEp3g9gh7F8bwtPTRAkASmNrrftmx4AxDKD5K4zDnr']);
const textEncoder = new (require('util').TextEncoder);
const textDecoder = new (require('util').TextDecoder)('utf-8', { fatal: true });
const api = new eosjs.Api({ rpc, signatureProvider, chainId: null, textEncoder, textDecoder });
const abiTypes = eosjs_ser.getTypesFromAbi(eosjs_ser.createInitialTypes(), abiAbi);
const js2Types = eosjs_ser.getTypesFromAbi(eosjs_ser.createInitialTypes(), transactionAbi);

function eosjs_hex_abi_to_json(hex: string): any {
    return api.rawAbiToJson(eosjs_ser.hexToUint8Array(hex));
}

function eosjs_json_abi_to_hex(abi: any) {
    let buf = new eosjs_ser.SerialBuffer({ textEncoder, textDecoder });
    abiTypes.get("abi_def").serialize(buf, {
        "types": [],
        "actions": [],
        "tables": [],
        "structs": [],
        "ricardian_clauses": [],
        "error_messages": [],
        "abi_extensions": [],
        ...abi
    });
    return eosjs_ser.arrayToHex(buf.asUint8Array());
}

function abieos_json_to_hex(contract: number, type: string, data: string) {
    check(l.abieos_json_to_bin(context, contract, cstr(type), cstr(data)));
    return l.abieos_get_bin_hex(context).readCString();
}

function abieos_hex_to_json(contract: number, type: string, hex: string) {
    let result = l.abieos_hex_to_json(context, contract, cstr(type), cstr(hex));
    checkPtr(result);
    return result.readCString();
}

function eosjs_json_to_hex(types: any, type: string, data: any) {
    let js2Type = eosjs_ser.getType(types, type);
    let buf = new eosjs_ser.SerialBuffer({ textEncoder, textDecoder });
    js2Type.serialize(buf, data);
    return eosjs_ser.arrayToHex(buf.asUint8Array());
}

function eosjs_hex_to_json(types: any, type: string, hex: string) {
    let js2Type = eosjs_ser.getType(types, type);
    let buf = new eosjs_ser.SerialBuffer({ textEncoder, textDecoder, array: eosjs_ser.hexToUint8Array(hex) });
    return js2Type.deserialize(buf);
}

function check_throw(message: string, f: () => void) {
    try {
        f();
    } catch (e) {
        if (e + '' === message)
            return;
        throw new Error(`expected exception: ${message}, got: ${e + ''}`);
    }
    throw new Error(`expected exception: ${message}`);
}

function check_type(contract: number, types: any, type: string, data: string, expected = data) {
    let hex = abieos_json_to_hex(contract, type, data);
    let json = abieos_hex_to_json(contract, type, hex);
    console.log(type, data, hex, json);
    if (json !== expected)
        throw new Error('conversion mismatch');
    json = JSON.stringify(JSON.parse(json));

    //console.log(type, data);
    let js2Type = eosjs_ser.getType(types, type);
    let buf = new eosjs_ser.SerialBuffer({ textEncoder, textDecoder });
    js2Type.serialize(buf, JSON.parse(data));
    let js2Hex = eosjs_ser.arrayToHex(buf.asUint8Array()).toUpperCase();
    //console.log(hex)
    //console.log(js2Hex)
    if (js2Hex != hex)
        throw new Error('eosjs hex mismatch');
    let js2Json = JSON.stringify(js2Type.deserialize(buf));
    //console.log(json);
    //console.log(js2Json);
    if (js2Json != json)
        throw new Error('eosjs json mismatch');
}

function check_types() {
    let token = name('eosio.token');
    let test = name('test.abi');
    check(l.abieos_set_abi_hex(context, token, cstr(tokenHexApi)));
    check(l.abieos_set_abi(context, test, cstr(testAbi)));
    const tokenTypes = eosjs_ser.getTypesFromAbi(eosjs_ser.createInitialTypes(), eosjs_hex_abi_to_json(tokenHexApi));
    const testTypes = eosjs_ser.getTypesFromAbi(eosjs_ser.createInitialTypes(), eosjs_hex_abi_to_json(eosjs_json_abi_to_hex(JSON.parse(testAbi))));

    check_throw('Error: missing abi_def.version (type=string)', () => eosjs_hex_abi_to_json(eosjs_json_abi_to_hex({})));
    check_throw('Error: Unsupported abi version', () => eosjs_hex_abi_to_json(eosjs_json_abi_to_hex({ version: '' })));
    check_throw('Error: Unsupported abi version', () => eosjs_hex_abi_to_json(eosjs_json_abi_to_hex({ version: 'eosio::abi/9.0' })));
    eosjs_hex_abi_to_json(eosjs_json_abi_to_hex({ version: 'eosio::abi/1.0' }));
    eosjs_hex_abi_to_json(eosjs_json_abi_to_hex({ version: 'eosio::abi/1.1' }));

    check_type(0, js2Types, 'bool', 'true');
    check_type(0, js2Types, 'bool', 'false');
    check_throw('Error: Read past end of buffer', () => eosjs_hex_to_json(js2Types, 'bool', ''));
    check_throw('Error: Expected true or false', () => eosjs_json_to_hex(js2Types, 'bool', 'trues'));
    check_throw('Error: Expected true or false', () => eosjs_json_to_hex(js2Types, 'bool', null));
    check_type(0, js2Types, 'int8', '0');
    check_type(0, js2Types, 'int8', '127');
    check_type(0, js2Types, 'int8', '-128');
    check_type(0, js2Types, 'uint8', '0');
    check_type(0, js2Types, 'uint8', '1');
    check_type(0, js2Types, 'uint8', '254');
    check_type(0, js2Types, 'uint8', '255');
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int8', 128));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int8', -129));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint8', -1));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint8', 256));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int8', '128'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int8', '-129'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint8', '-1'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint8', '256'));
    check_type(0, js2Types, 'uint8[]', '[]');
    check_type(0, js2Types, 'uint8[]', '[10]');
    check_type(0, js2Types, 'uint8[]', '[10,9]');
    check_type(0, js2Types, 'uint8[]', '[10,9,8]');
    check_type(0, js2Types, 'int16', '0');
    check_type(0, js2Types, 'int16', '32767');
    check_type(0, js2Types, 'int16', '-32768');
    check_throw('Error: Read past end of buffer', () => eosjs_hex_to_json(js2Types, 'int16', '01'));
    check_type(0, js2Types, 'uint16', '0');
    check_type(0, js2Types, 'uint16', '65535');
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int16', 32768));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int16', -32769));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint16', -1));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint16', 655356));
    check_type(0, js2Types, 'int32', '0');
    check_type(0, js2Types, 'int32', '2147483647');
    check_type(0, js2Types, 'int32', '-2147483648');
    check_type(0, js2Types, 'uint32', '0');
    check_type(0, js2Types, 'uint32', '4294967295');
    check_throw('Error: Expected number', () => eosjs_json_to_hex(js2Types, 'int32', 'foo'));
    check_throw('Error: Expected number', () => eosjs_json_to_hex(js2Types, 'int32', true));
    check_throw('Error: Expected number', () => eosjs_json_to_hex(js2Types, 'int32', []));
    check_throw('Error: Expected number', () => eosjs_json_to_hex(js2Types, 'int32', {}));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int32', '2147483648'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'int32', '-2147483649'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint32', '-1'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'uint32', '4294967296'));
    check_type(0, js2Types, 'int64', '0', '"0"');
    check_type(0, js2Types, 'int64', '1', '"1"');
    check_type(0, js2Types, 'int64', '-1', '"-1"');
    check_type(0, js2Types, 'int64', '"0"');
    check_type(0, js2Types, 'int64', '"9223372036854775807"');
    check_type(0, js2Types, 'int64', '"-9223372036854775808"');
    check_type(0, js2Types, 'uint64', '"0"');
    check_type(0, js2Types, 'uint64', '"18446744073709551615"');
    check_throw('Error: number is out of range', () => eosjs_json_to_hex(js2Types, 'int64', '9223372036854775808'));
    check_throw('Error: number is out of range', () => eosjs_json_to_hex(js2Types, 'int64', '-9223372036854775809'));
    check_throw('Error: invalid number', () => eosjs_json_to_hex(js2Types, 'uint64', '-1'));
    check_throw('Error: number is out of range', () => eosjs_json_to_hex(js2Types, 'uint64', '18446744073709551616'));
    check_type(0, js2Types, 'int128', '"0"');
    check_type(0, js2Types, 'int128', '"1"');
    check_type(0, js2Types, 'int128', '"-1"');
    check_type(0, js2Types, 'int128', '"18446744073709551615"');
    check_type(0, js2Types, 'int128', '"-18446744073709551615"');
    check_type(0, js2Types, 'int128', '"170141183460469231731687303715884105727"');
    check_type(0, js2Types, 'int128', '"-170141183460469231731687303715884105727"');
    check_type(0, js2Types, 'int128', '"-170141183460469231731687303715884105728"');
    check_type(0, js2Types, 'uint128', '"0"');
    check_type(0, js2Types, 'uint128', '"1"');
    check_type(0, js2Types, 'uint128', '"18446744073709551615"');
    check_type(0, js2Types, 'uint128', '"340282366920938463463374607431768211454"');
    check_type(0, js2Types, 'uint128', '"340282366920938463463374607431768211455"');
    check_throw('Error: number is out of range', () => eosjs_json_to_hex(js2Types, 'int128', '170141183460469231731687303715884105728'));
    check_throw('Error: number is out of range', () => eosjs_json_to_hex(js2Types, 'int128', '-170141183460469231731687303715884105729'));
    check_throw('Error: invalid number', () => eosjs_json_to_hex(js2Types, 'int128', 'true'));
    check_throw('Error: invalid number', () => eosjs_json_to_hex(js2Types, 'uint128', '-1'));
    check_throw('Error: number is out of range', () => eosjs_json_to_hex(js2Types, 'uint128', '340282366920938463463374607431768211456'));
    check_throw('Error: invalid number', () => eosjs_json_to_hex(js2Types, 'uint128', 'true'));
    check_type(0, js2Types, 'varuint32', '0');
    check_type(0, js2Types, 'varuint32', '127');
    check_type(0, js2Types, 'varuint32', '128');
    check_type(0, js2Types, 'varuint32', '129');
    check_type(0, js2Types, 'varuint32', '16383');
    check_type(0, js2Types, 'varuint32', '16384');
    check_type(0, js2Types, 'varuint32', '16385');
    check_type(0, js2Types, 'varuint32', '2097151');
    check_type(0, js2Types, 'varuint32', '2097152');
    check_type(0, js2Types, 'varuint32', '2097153');
    check_type(0, js2Types, 'varuint32', '268435455');
    check_type(0, js2Types, 'varuint32', '268435456');
    check_type(0, js2Types, 'varuint32', '268435457');
    check_type(0, js2Types, 'varuint32', '4294967294');
    check_type(0, js2Types, 'varuint32', '4294967295');
    check_type(0, js2Types, 'varint32', '0');
    check_type(0, js2Types, 'varint32', '-1');
    check_type(0, js2Types, 'varint32', '1');
    check_type(0, js2Types, 'varint32', '-2');
    check_type(0, js2Types, 'varint32', '2');
    check_type(0, js2Types, 'varint32', '-2147483647');
    check_type(0, js2Types, 'varint32', '2147483647');
    check_type(0, js2Types, 'varint32', '-2147483648');
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'varint32', '2147483648'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'varint32', '-2147483649'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'varuint32', '-1'));
    check_throw('Error: Number is out of range', () => eosjs_json_to_hex(js2Types, 'varuint32', '4294967296'));
    check_type(0, js2Types, 'float32', '0.0');
    check_type(0, js2Types, 'float32', '0.125');
    check_type(0, js2Types, 'float32', '-0.125');
    check_type(0, js2Types, 'float64', '0.0');
    check_type(0, js2Types, 'float64', '0.125');
    check_type(0, js2Types, 'float64', '-0.125');
    check_type(0, js2Types, 'float128', '"00000000000000000000000000000000"');
    check_type(0, js2Types, 'float128', '"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"');
    check_type(0, js2Types, 'float128', '"12345678ABCDEF12345678ABCDEF1234"');
    check_type(0, js2Types, 'time_point_sec', '"1970-01-01T00:00:00.000"');
    check_type(0, js2Types, 'time_point_sec', '"2018-06-15T19:17:47.000"');
    check_type(0, js2Types, 'time_point_sec', '"2060-06-15T19:17:47.000"');
    check_throw('Error: Invalid time format', () => eosjs_json_to_hex(js2Types, 'time_point_sec', true));
    check_type(0, js2Types, 'time_point', '"1970-01-01T00:00:00.000"');
    check_type(0, js2Types, 'time_point', '"1970-01-01T00:00:00.001"');
    check_type(0, js2Types, 'time_point', '"1970-01-01T00:00:00.002"');
    check_type(0, js2Types, 'time_point', '"1970-01-01T00:00:00.010"');
    check_type(0, js2Types, 'time_point', '"1970-01-01T00:00:00.100"');
    check_type(0, js2Types, 'time_point', '"2018-06-15T19:17:47.000"');
    check_type(0, js2Types, 'time_point', '"2018-06-15T19:17:47.999"');
    check_type(0, js2Types, 'time_point', '"2060-06-15T19:17:47.999"');
    check_throw('Error: Invalid time format', () => eosjs_json_to_hex(js2Types, 'time_point', true));
    check_type(0, js2Types, 'block_timestamp_type', '"2000-01-01T00:00:00.000"');
    check_type(0, js2Types, 'block_timestamp_type', '"2000-01-01T00:00:00.500"');
    check_type(0, js2Types, 'block_timestamp_type', '"2000-01-01T00:00:01.000"');
    check_type(0, js2Types, 'block_timestamp_type', '"2018-06-15T19:17:47.500"');
    check_type(0, js2Types, 'block_timestamp_type', '"2018-06-15T19:17:48.000"');
    check_throw('Error: Invalid time format', () => eosjs_json_to_hex(js2Types, 'block_timestamp_type', true));
    check_type(0, js2Types, 'name', '""');
    check_type(0, js2Types, 'name', '"1"');
    check_type(0, js2Types, 'name', '"abcd"');
    check_type(0, js2Types, 'name', '"ab.cd.ef"');
    check_type(0, js2Types, 'name', '"ab.cd.ef.1234"');
    check_type(0, js2Types, 'name', '"..ab.cd.ef.."', '"..ab.cd.ef"');
    check_type(0, js2Types, 'name', '"zzzzzzzzzzzz"');
    check_type(0, js2Types, 'name', '"zzzzzzzzzzzzz"', '"zzzzzzzzzzzzj"');
    check_throw('Error: Expected string containing name', () => eosjs_json_to_hex(js2Types, 'name', true));
    check_type(0, js2Types, 'bytes', '""');
    check_type(0, js2Types, 'bytes', '"00"');
    check_type(0, js2Types, 'bytes', '"AABBCCDDEEFF00010203040506070809"');
    check_throw('Error: Odd number of hex digits', () => eosjs_json_to_hex(js2Types, 'bytes', '"0"'));
    check_throw('Error: Expected hex string', () => eosjs_json_to_hex(js2Types, 'bytes', '"yz"'));
    check_throw('Error: Expected string containing hex digits', () => eosjs_json_to_hex(js2Types, 'bytes', true));
    check_throw('Error: Read past end of buffer', () => eosjs_hex_to_json(js2Types, 'bytes', "01"));
    check_type(0, js2Types, 'string', '""');
    check_type(0, js2Types, 'string', '"z"');
    check_type(0, js2Types, 'string', '"This is a string."');
    check_type(0, js2Types, 'string', '"' + '*'.repeat(128) + '"');
    check_type(0, js2Types, 'string', `"\\u0000  这是一个测试  Это тест  هذا اختبار 👍"`);
    check_throw('Error: Read past end of buffer', () => eosjs_hex_to_json(js2Types, 'string', '01'));
    check_type(0, js2Types, 'checksum160', '"0000000000000000000000000000000000000000"');
    check_type(0, js2Types, 'checksum160', '"123456789ABCDEF01234567890ABCDEF70123456"');
    check_type(0, js2Types, 'checksum256', '"0000000000000000000000000000000000000000000000000000000000000000"');
    check_type(0, js2Types, 'checksum256', '"0987654321ABCDEF0987654321FFFF1234567890ABCDEF001234567890ABCDEF"');
    check_type(0, js2Types, 'checksum512', '"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"');
    check_type(0, js2Types, 'checksum512', '"0987654321ABCDEF0987654321FFFF1234567890ABCDEF001234567890ABCDEF0987654321ABCDEF0987654321FFFF1234567890ABCDEF001234567890ABCDEF"');
    check_throw('Error: Expected hex string', () => eosjs_json_to_hex(js2Types, 'checksum256', 'yz'));
    check_throw('Error: Expected string containing hex digits', () => eosjs_json_to_hex(js2Types, 'checksum256', true));
    check_throw('Error: Binary data has incorrect size', () => eosjs_json_to_hex(js2Types, 'checksum256', 'a0'));
    check_type(0, js2Types, 'public_key', '"EOS1111111111111111111111111111111114T1Anm"', '"PUB_K1_11111111111111111111111111111111149Mr2R"');
    check_type(0, js2Types, 'public_key', '"EOS11111111111111111111111115qCHTcgbQwptSz99m"', '"PUB_K1_11111111111111111111111115qCHTcgbQwpvP72Uq"');
    check_type(0, js2Types, 'public_key', '"EOS111111111111111114ZrjxJnU1LA5xSyrWMNuXTrYSJ57"', '"PUB_K1_111111111111111114ZrjxJnU1LA5xSyrWMNuXTrVub2r"');
    check_type(0, js2Types, 'public_key', '"EOS1111111113diW7pnisfdBvHTXP7wvW5k5Ky1e5DVuF23dosU"', '"PUB_K1_1111111113diW7pnisfdBvHTXP7wvW5k5Ky1e5DVuF4PizpM"');
    check_type(0, js2Types, 'public_key', '"EOS11DsZ6Lyr1aXpm9aBqqgV4iFJpNbSw5eE9LLTwNAxqjJgmjgbT"', '"PUB_K1_11DsZ6Lyr1aXpm9aBqqgV4iFJpNbSw5eE9LLTwNAxqjJgXSdB8"');
    check_type(0, js2Types, 'public_key', '"EOS12wkBET2rRgE8pahuaczxKbmv7ciehqsne57F9gtzf1PVYNMRa2"', '"PUB_K1_12wkBET2rRgE8pahuaczxKbmv7ciehqsne57F9gtzf1PVb7Rf7o"');
    check_type(0, js2Types, 'public_key', '"EOS1yp8ebBuKZ13orqUrZsGsP49e6K3ThVK1nLutxSyU5j9SaXz9a"', '"PUB_K1_1yp8ebBuKZ13orqUrZsGsP49e6K3ThVK1nLutxSyU5j9Tx1r96"');
    check_type(0, js2Types, 'public_key', '"EOS9adaAMuB9v8yX1mZ5PtoB6VFSCeqRGjASd8ZTM6VUkiHL7mue4K"', '"PUB_K1_9adaAMuB9v8yX1mZ5PtoB6VFSCeqRGjASd8ZTM6VUkiHLB5XEdw"');
    check_type(0, js2Types, 'public_key', '"EOS69X3383RzBZj41k73CSjUNXM5MYGpnDxyPnWUKPEtYQmTBWz4D"', '"PUB_K1_69X3383RzBZj41k73CSjUNXM5MYGpnDxyPnWUKPEtYQmVzqTY7"');
    check_type(0, js2Types, 'public_key', '"EOS7yBtksm8Kkg85r4in4uCbfN77uRwe82apM8jjbhFVDgEgz3w8S"', '"PUB_K1_7yBtksm8Kkg85r4in4uCbfN77uRwe82apM8jjbhFVDgEcarGb8"');
    check_type(0, js2Types, 'public_key', '"EOS7WnhaKwHpbSidYuh2DF1qAExTRUtPEdZCaZqt75cKcixuQUtdA"', '"PUB_K1_7WnhaKwHpbSidYuh2DF1qAExTRUtPEdZCaZqt75cKcixtU7gEn"');
    check_type(0, js2Types, 'public_key', '"EOS7Bn1YDeZ18w2N9DU4KAJxZDt6hk3L7eUwFRAc1hb5bp6xJwxNV"', '"PUB_K1_7Bn1YDeZ18w2N9DU4KAJxZDt6hk3L7eUwFRAc1hb5bp6uEBZA8"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_11111111111111111111111111111111149Mr2R"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_11111111111111111111111115qCHTcgbQwpvP72Uq"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_111111111111111114ZrjxJnU1LA5xSyrWMNuXTrVub2r"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_1111111113diW7pnisfdBvHTXP7wvW5k5Ky1e5DVuF4PizpM"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_11DsZ6Lyr1aXpm9aBqqgV4iFJpNbSw5eE9LLTwNAxqjJgXSdB8"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_12wkBET2rRgE8pahuaczxKbmv7ciehqsne57F9gtzf1PVb7Rf7o"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_1yp8ebBuKZ13orqUrZsGsP49e6K3ThVK1nLutxSyU5j9Tx1r96"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_9adaAMuB9v8yX1mZ5PtoB6VFSCeqRGjASd8ZTM6VUkiHLB5XEdw"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_69X3383RzBZj41k73CSjUNXM5MYGpnDxyPnWUKPEtYQmVzqTY7"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_7yBtksm8Kkg85r4in4uCbfN77uRwe82apM8jjbhFVDgEcarGb8"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_7WnhaKwHpbSidYuh2DF1qAExTRUtPEdZCaZqt75cKcixtU7gEn"');
    check_type(0, js2Types, 'public_key', '"PUB_K1_7Bn1YDeZ18w2N9DU4KAJxZDt6hk3L7eUwFRAc1hb5bp6uEBZA8"');
    check_type(0, js2Types, 'public_key', '"PUB_R1_1111111111111111111111111111111116amPNj"');
    check_type(0, js2Types, 'public_key', '"PUB_R1_67vQGPDMCR4gbqYV3hkfNz3BfzRmmSj27kFDKrwDbaZKtaX36u"');
    check_type(0, js2Types, 'public_key', '"PUB_R1_6FPFZqw5ahYrR9jD96yDbbDNTdKtNqRbze6oTDLntrsANgQKZu"');
    check_type(0, js2Types, 'public_key', '"PUB_R1_7zetsBPJwGQqgmhVjviZUfoBMktHinmTqtLczbQqrBjhaBgi6x"');
    check_type(0, js2Types, 'public_key', '"PUB_WA_8PPYTWYNkRqrveNAoX7PJWDtSqDUp3c29QGBfr6MD9EaLocaPBmsk5QAHWq4vEQt2"');
    check_type(0, js2Types, 'public_key', '"PUB_WA_6VFnP5vnq1GjNyMR7S17e2yp6SRoChiborF2LumbnXvMTsPASXykJaBBGLhprXTpk"');
    check_throw('Error: expected string containing public key', () => eosjs_json_to_hex(js2Types, 'public_key', true));
    check_throw('Error: unrecognized public key format', () => eosjs_json_to_hex(js2Types, 'public_key', 'foo'));
    check_type(0, js2Types, 'private_key', '"PVT_R1_PtoxLPzJZURZmPS4e26pjBiAn41mkkLPrET5qHnwDvbvqFEL6"');
    check_type(0, js2Types, 'private_key', '"PVT_R1_vbRKUuE34hjMVQiePj2FEjM8FvuG7yemzQsmzx89kPS9J8Coz"');
    check_throw('Error: expected string containing private key', () => eosjs_json_to_hex(js2Types, 'private_key', true));
    check_throw('Error: unrecognized private key type', () => eosjs_json_to_hex(js2Types, 'private_key', 'foo'));
    check_type(0, js2Types, 'signature', '"SIG_K1_Kg2UKjXTX48gw2wWH4zmsZmWu3yarcfC21Bd9JPj7QoDURqiAacCHmtExPk3syPb2tFLsp1R4ttXLXgr7FYgDvKPC5RCkx"');
    check_type(0, js2Types, 'signature', '"SIG_R1_Kfh19CfEcQ6pxkMBz6xe9mtqKuPooaoyatPYWtwXbtwHUHU8YLzxPGvZhkqgnp82J41e9R6r5mcpnxy1wAf1w9Vyo9wybZ"');
    check_type(0, js2Types, 'signature', '"SIG_WA_FjWGWXz7AC54NrVWXS8y8DGu1aesCr7oFiFmVg4a1QfNS74JwaVkqkN8xbMD64uvcsmPvtNnA9du6G6nSsWuyT9tM8CQw9mV1BSbWEs8hjF1uFBP1QHAEadvhkZQPU1FTyPMz4jevaHYMQgfMiAf3QoPhPn9RGxzvNph8Zrd6F3pKpZkUe92tGQU8PQvEMa22ELPvdXzxXC6qUKnKVSH4gK7BXw168jb5d3nnWrpQ1yrLTWB4xizEMpN8sTfsgScKKx1QajX2uNUahQEb1cxipQZbVMApifHEUsK45PqsNxfXvb"');
    check_type(0, js2Types, 'signature', '"SIG_WA_FejsRu4VrdwoZ27v2D3wmp4Kge46JJSqWsiMgbJapVuuYnPDyZZjJSTggdHUNPMp3zt2fGfAdpWY7ScsohZzWTJ1iTerbab2pNE6Tso7MJRjdMAG56K4fjrASEK6QsUs7rxG9Syp7kstBcq8eZidayrtK9YSH1MCNTAqrDPMbN366vR8q5XeN5BSDmyDsqmjsMMSKWMeEbUi7jNHKLziZY6dKHNqDYqjmDmuXoevxyDRWrNVHjAzvBtfTuVtj2r5tCScdCZ3a7yQ1D2zZvstphB4t5HN9YXw1HGS3yKCY6uRZ2V"');
    check_throw('Error: expected string containing signature', () => eosjs_json_to_hex(js2Types, 'signature', true));
    check_throw('Error: unrecognized signature format', () => eosjs_json_to_hex(js2Types, 'signature', 'foo'));
    check_type(0, js2Types, 'symbol_code', '"A"');
    check_type(0, js2Types, 'symbol_code', '"B"');
    check_type(0, js2Types, 'symbol_code', '"SYS"');
    check_throw('Error: Expected string containing symbol_code', () => eosjs_json_to_hex(js2Types, 'symbol_code', true));
    check_type(0, js2Types, 'symbol', '"0,A"');
    check_type(0, js2Types, 'symbol', '"1,Z"');
    check_type(0, js2Types, 'symbol', '"4,SYS"');
    check_throw('Error: Expected string containing symbol', () => eosjs_json_to_hex(js2Types, 'symbol', null));
    check_type(0, js2Types, 'asset', '"0 FOO"');
    check_type(0, js2Types, 'asset', '"0.0 FOO"');
    check_type(0, js2Types, 'asset', '"0.00 FOO"');
    check_type(0, js2Types, 'asset', '"0.000 FOO"');
    check_type(0, js2Types, 'asset', '"1.2345 SYS"');
    check_type(0, js2Types, 'asset', '"-1.2345 SYS"');
    check_throw('Error: Expected string containing asset', () => eosjs_json_to_hex(js2Types, 'asset', null));
    check_type(0, js2Types, 'asset[]', '[]');
    check_type(0, js2Types, 'asset[]', '["0 FOO"]');
    check_type(0, js2Types, 'asset[]', '["0 FOO","0.000 FOO"]');
    check_type(0, js2Types, 'asset?', 'null');
    check_type(0, js2Types, 'asset?', '"0.123456 SIX"');
    check_type(0, js2Types, 'extended_asset', '{"quantity":"0 FOO","contract":"bar"}');
    check_type(0, js2Types, 'extended_asset', '{"quantity":"0.123456 SIX","contract":"seven"}');

    check_type(token, tokenTypes, "transfer", '{"from":"useraaaaaaaa","to":"useraaaaaaab","quantity":"0.0001 SYS","memo":"test memo"}');
    check_type(0, js2Types, "transaction", '{"expiration":"2009-02-13T23:31:31.000","ref_block_num":1234,"ref_block_prefix":5678,"max_net_usage_words":0,"max_cpu_usage_ms":0,"delay_sec":0,"context_free_actions":[],"actions":[{"account":"eosio.token","name":"transfer","authorization":[{"actor":"useraaaaaaaa","permission":"active"}],"data":"608C31C6187315D6708C31C6187315D60100000000000000045359530000000000"}],"transaction_extensions":[]}');

    check_type(test, testTypes, "v1", '["int8",7]');
    check_type(test, testTypes, "v1", '["s1",{"x1":6}]');
    check_type(test, testTypes, "v1", '["s2",{"y1":5,"y2":4}]');

    check_type(test, testTypes, "s3", '{}');
    check_type(test, testTypes, "s3", '{"z1":7}');
    check_type(test, testTypes, "s3", '{"z1":7,"z2":["int8",6]}');
    check_type(test, testTypes, "s3", '{"z1":7,"z2":["int8",6],"z3":{}}', '{"z1":7,"z2":["int8",6]}');
    check_type(test, testTypes, "s3", '{"z1":7,"z2":["int8",6],"z3":{"y1":9}}');
    check_type(test, testTypes, "s3", '{"z1":7,"z2":["int8",6],"z3":{"y1":9,"y2":10}}');

    check_type(test, testTypes, "s4", '{}');
    check_type(test, testTypes, "s4", '{"a1":null}');
    check_type(test, testTypes, "s4", '{"a1":7}');
    check_type(test, testTypes, "s4", '{"a1":null,"b1":[]}');
    check_type(test, testTypes, "s4", '{"a1":null,"b1":[5,6,7]}');
}

async function push_transfer() {
    if (useTokenHexApi)
        check(l.abieos_set_abi_hex(context, name('eosio.token'), cstr(tokenHexApi)));
    else
        check(l.abieos_set_abi(context, name('eosio.token'), jsonStr((await rpc.get_abi('eosio.token')).abi)));
    let type = checkPtr(l.abieos_get_type_for_action(context, name('eosio.token'), name('transfer')));
    check(l.abieos_json_to_bin(context, name('eosio.token'), type, jsonStr({
        from: 'useraaaaaaaa',
        to: 'useraaaaaaab',
        quantity: '0.0001 SYS',
        memo: '',
    })));
    const actionDataHex = l.abieos_get_bin_hex(context).readCString();
    console.log('action json->bin: ', actionDataHex);
    console.log('action bin->json: ', abieos_hex_to_json(name('eosio.token'), 'transfer', actionDataHex));

    let info = await rpc.get_info();
    let refBlock = await rpc.get_block(info.head_block_num - 3);
    let transaction = {
        expiration: eosjs_ser.timePointSecToDate(eosjs_ser.dateToTimePointSec(refBlock.timestamp) + 10),
        ref_block_num: refBlock.block_num,
        ref_block_prefix: refBlock.ref_block_prefix,
        max_net_usage_words: 0,
        max_cpu_usage_ms: 0,
        delay_sec: 0,
        context_free_actions: [] as any,
        actions: [{
            account: 'eosio.token',
            name: 'transfer',
            authorization: [{
                actor: 'useraaaaaaaa',
                permission: 'active',
            }],
            data: actionDataHex,
        }],
        transaction_extensions: [] as any,
    };
    check(l.abieos_json_to_bin(context, 0, cstr('transaction'), jsonStr(transaction)));
    let transactionDataHex = l.abieos_get_bin_hex(context).readCString();
    console.log('transaction json->bin: ', transactionDataHex);
    console.log('transaction bin->json: ', abieos_hex_to_json(0, 'transaction', transactionDataHex));

    let sig = await signatureProvider.sign({
        chainId: info.chain_id,
        requiredKeys: await signatureProvider.getAvailableKeys(),
        serializedTransaction: eosjs_ser.hexToUint8Array(transactionDataHex),
        abis: [],
    });
    console.log('sig:', sig)

    let result = await rpc.fetch('/v1/chain/push_transaction', {
        signatures: sig,
        compression: 0,
        packed_context_free_data: '',
        packed_trx: transactionDataHex,
    });
    console.log(JSON.stringify(result, null, 4));
}

(async () => {
    try {
        check(context);
        check(l.abieos_set_abi(context, 0, jsonStr(transactionAbi)));
        check_types();
        if (useRpcEndpoint)
            await push_transfer();
    } catch (e) {
        console.log(e);
    }
})();
