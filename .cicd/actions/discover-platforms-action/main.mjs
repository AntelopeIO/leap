import core from '@actions/core';
import github from '@actions/github';
import crypto from 'node:crypto';
import axios from 'axios';

const token = core.getInput('password', {required: true});
const packageName = core.getInput('package-name', {required: true});

function hexhashOfURL(url) {
   return new Promise((resolve, reject) => {
      axios.get(url, {responseType:"stream", headers:{"Authorization": `Bearer ${token}`}}).then((resp) => {
         resp.data.pipe(crypto.createHash('sha256')).setEncoding('hex').on('data', (hexhash) => {
            resolve(hexhash);
         });
      });
   });
}

function imageLabelExists(hexhash) {
   return new Promise((resolve, reject) => {
      axios.head(`https://ghcr.io/v2/${github.context.repo.owner.toLowerCase()}/${packageName}/manifests/${hexhash}`, {validateStatus:null, headers:{"Authorization":`Bearer ${Buffer.from(token).toString('base64')}`}}).then((resp) => {
         switch(resp.status) {
            case 404:
               resolve(false);
               break;
            case 200:
               resolve(true);
               break;
            default:
               reject(new Error(`Getting package manifest resulted in status ${resp.status}`));
         }
      });
   });
}

try {
   const urlBase = `https://raw.githubusercontent.com/${github.context.repo.owner.toLowerCase()}/${github.context.repo.repo}/${github.context.sha}`;
   let platforms = (await axios.get(`${urlBase}/${core.getInput('platform-file', {required: true})}`, {headers:{"Authorization": `Bearer ${core.getInput('password')}`}})).data;

   let missingPlatforms = [];  //platforms that need to be rebuilt
   let queryPromises = [];

   for(const [platform, {dockerfile}] of Object.entries(platforms)) {
      queryPromises.push(hexhashOfURL(`${urlBase}/${dockerfile}`).then((hexhash) => {
         platforms[platform].image = `ghcr.io/${github.context.repo.owner.toLowerCase()}/${packageName}:${hexhash}`;
         return imageLabelExists(hexhash).then((exists) => {
            if(!exists)
            missingPlatforms.push(platform);
         });
      }));
   }

   await Promise.all(queryPromises);

   core.setOutput('missing-platforms', JSON.stringify(missingPlatforms));
   core.setOutput('platforms', JSON.stringify(platforms));
} catch (error) {
   core.setFailed(error.message);
}
