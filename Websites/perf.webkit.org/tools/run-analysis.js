#!/usr/local/bin/node

const fs = require('fs');
const parseArguments = require('./js/parse-arguments.js').parseArguments;
const RemoteAPI = require('./js/remote.js').RemoteAPI;
const MeasurementSetAnalyzer = require('./js/measurement-set-analyzer').MeasurementSetAnalyzer;
require('./js/v3-models.js');
global.PrivilegedAPI = require('./js/privileged-api.js').PrivilegedAPI;

function main(argv)
{
    const options = parseArguments(argv, [
        {name: '--server-config-json', required: true},
        {name: '--analysis-range-in-days', type: parseFloat, default: 10},
        {name: '--seconds-to-sleep', type: parseFloat, default: 1200},
    ]);

    if (!options)
        return;

    analysisLoop(options);
}

async function analysisLoop(options)
{
    let secondsToSleep;
    try {
        const serverConfig = JSON.parse(fs.readFileSync(options['--server-config-json'], 'utf-8'));
        const analysisRangeInDays = options['--analysis-range-in-days'];
        secondsToSleep = options['--seconds-to-sleep'];
        global.RemoteAPI = new RemoteAPI(serverConfig.server);
        PrivilegedAPI.configure(serverConfig.slave.name, serverConfig.slave.password);

        const manifest = await Manifest.fetch();
        const measurementSetList = MeasurementSetAnalyzer.measurementSetListForAnalysis(manifest);

        const endTime = Date.now();
        const startTime = endTime - analysisRangeInDays * 24 * 3600 * 1000;
        const analyzer = new MeasurementSetAnalyzer(measurementSetList, startTime, endTime, console);

        console.log(`Start analyzing last ${analysisRangeInDays} days measurement sets.`);
        await analyzer.analyzeOnce();
    } catch(error) {
        console.error(`Failed analyze measurement sets due to ${error}`);
    }

    console.log(`Sleeping for ${secondsToSleep} seconds.`);
    setTimeout(() => analysisLoop(options), secondsToSleep * 1000);
}


main(process.argv);