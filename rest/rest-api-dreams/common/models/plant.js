'use strict';
var _ = require('lodash');
var docker = require('../utils/docker');

module.exports = function(Plant) {
  Plant.observe('before save', (ctx, next) => {
    const newPlant = (ctx.instance || ctx.data);
    const { gatewayId, dnp3Address, plantNo } = newPlant;

    // Assign dnp3Address
    let promise;
    if (plantNo && !dnp3Address) {
      promise = Plant.find({where: {gatewayId}, order: 'dnp3Address DESC', limit: 1}).then(plants => {
        const lastAddress = _.get(plants, '[0].dnp3Address');
        newPlant.dnp3Address = lastAddress ? lastAddress + 1 : 4;
      });
    } else {
      promise = Promise.resolve();
    }

    promise.then(() => {
      next();
    });
  })

  Plant.remoteMethod('plantMeterNo', {
    http: {path: '/plantMeterNo/:plantNo', verb: 'get'},
    accepts: [
      { arg: 'plantNo', type: 'string', required: true, description: 'plantNo' },
      { arg: 'options', type: 'object', http: 'optionsFromRequest' },
    ],
    returns: { arg: 'data', type: 'object', root: true },
  });

  Plant.plantMeterNo = async function(plantNo, options) {
    const models = Plant.app.models;
    const plants = await Plant.find({'where': {plantNo}});
    if (_.isEmpty(plants)) {
      throw {status: 404, message: `No plant found for plantNo ${plantNo}`};
    }
    const { gatewayId: id } = plants[0].toObject();
    const siteToken = options.accessToken.id;
    const gateway = await models.Gateway.findOne({'where': {id, siteToken}, 'include': ['plant']});
    if (gateway === null) {
      throw {status: 404, message: `No gateway found for plant ${plantNo} with token ${siteToken}`};
    }
    return gateway.plant().map(({plantName, plantNo, dnp3Address}) => {
      return {plantName, plantNo, dnp3Address};
    });
  };

  Plant.remoteMethod('integrityPoll', {
    http: {path: '/integrityPoll', verb: 'post'},
    accepts: [
      {arg: 'plantNo', type: 'string', required: true, description: 'Plant Number'},
    ],
    returns: {arg: 'data', type: 'object'},
  });

  Plant.integrityPoll = async function(plantNo) {
    const models = Plant.app.models;
    const plant = await models.Plant.findOne({ where: { plantNo }, include: { relation: 'gateway' }});
    if (!plant) { return null; }

    const { gateway, dnp3Address } = plant.toObject();
    if (!gateway) { return null; }

    const { ipAddress } = gateway;
    const dnp3Master = await docker.getService('dnp3-master');
    const streams = await docker.execInsideContainer(
      dnp3Master,
      [
        '/dreams-master/bin/dreams-msg-sender',
        'poll', ipAddress, `${dnp3Address}`,
      ]
    );
    const output = streams.map(buffer => {buffer.toString('utf8');});

    return { output };
  }

  Plant.remoteMethod('powerControl', {
    http: {path: '/powerControl', verb: 'post'},
    accepts: [
      {arg: 'plantNo', type: 'string', required: true, description: 'Plant Number'},
      {arg: 'type', type: 'string', required: true, description: 'active_power | reactive_power | power_factor | vpset | autonomic_control'},
      {arg: 'value', type: 'number', required: true, description: 'P: 0 - 100, Q/PF: -100 - 100, Vpset: 105 - 109, Autonomic Control: 1 or 0'},
    ],
    returns: {arg: 'data', type: 'object'},
  });

  Plant.powerControl = async function(plantNo, type, value) {
    const point_mapping = {
      active_power: 1,
      reactive_power: 2,
      power_factor: 0,
      vpset: 3,
      autonomic_control: 4,
    }
    const models = Plant.app.models;
    const plant = await models.Plant.findOne({ where: { plantNo }, include: { relation: 'gateway' }});
    if (!plant) { return null; }

    const { gateway, dnp3Address } = plant.toObject();
    if (!gateway) { return null; }

    const { ipAddress } = gateway;
    const dnp3Master = await docker.getService('dnp3-master');
    const streams = await docker.execInsideContainer(
      dnp3Master,
      [
        '/dreams-master/bin/dreams-msg-sender',
        '1', ipAddress, `${point_mapping[type]}`, `${value}`, `${dnp3Address}`,
      ]
    );
    const output = streams.map(buffer => {buffer.toString('utf8');});

    return { output };
  }

  Plant.remoteMethod('setDeadband', {
    http: {path: '/setDeadband', verb: 'post'},
    accepts: [
      {arg: 'plantNo', type: 'string', required: true, description: 'Plant Number'},
      {arg: 'field', type: 'string', required: true, description: 'The field name without _deadband'},
      {arg: 'value', type: 'number', required: true, description: 'The deadband threshold, ex: set 0.025 for 2.5%'},
      {arg: 'plantCategory', type: 'string', required: true, description: 'grid or energyStorage'},
    ],
    returns: {arg: 'data', type: 'object'},
  });

  Plant.setDeadband = async function(plantNo, field, value, plantCategory) {
    const point_mapping_map = {
      grid: {
        currentPhaseA: 5,
        currentPhaseB: 6,
        currentPhaseC: 7,
        currentPhaseN: 8,
        voltagePhaseA: 9,
        voltagePhaseB: 10,
        voltagePhaseC: 11,
        P_SUM: 12,
        Q_SUM: 13,
        PF_AVG: 14,
        frequency: 15,
        irradiance: 16,
        wind_speed: 17,
      },
      energyStorage: {
        P_SUM: 7,
      }
    }
    const point_mapping = point_mapping_map[plantCategory];

    if (!(field in point_mapping)) {
      throw { statusCode: 400, message: `The specified field is not supported ${field}`};
    }

    const models = Plant.app.models;
    const plant = await models.Plant.findOne({ where: { plantNo }, include: { relation: 'gateway' }});
    if (!plant) { return null; }

    const { gateway, dnp3Address } = plant.toObject();
    if (!gateway) { return null; }

    const { ipAddress } = gateway;
    const dnp3Master = await docker.getService('dnp3-master');
    const streams = await docker.execInsideContainer(
      dnp3Master,
      [
        '/dreams-master/bin/dreams-msg-sender',
        '1', ipAddress, `${point_mapping[field]}`, `${value * 10000}`, `${dnp3Address}`,
      ]
    );
    const output = streams.map(buffer => {buffer.toString('utf8');});

    return { output };
  }
};
