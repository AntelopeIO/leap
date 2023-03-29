import child_process from 'node:child_process';
import process from 'node:process';
import stream from 'node:stream';
import fs from 'node:fs';
import zlib from 'node:zlib';
import tar from 'tar-stream';
import core from '@actions/core'

const container = core.getInput('container', {required: true});
const error_log_paths = JSON.parse(core.getInput('error-log-paths', {required: true}));
const log_tarball_prefix = core.getInput('log-tarball-prefix', {required: true});
const tests_label = core.getInput('tests-label', {required: true});
const test_timeout = core.getInput('test-timeout', {required: true});

try {
   if(child_process.spawnSync("docker", ["run", "--name", "base", "-v", `${process.cwd()}/build.tar.zst:/build.tar.zst`, "--workdir", "/__w/leap/leap", container, "sh", "-c", "zstdcat /build.tar.zst | tar x"], {stdio:"inherit"}).status)
      throw new Error("Failed to create base container");
   if(child_process.spawnSync("docker", ["commit", "base", "baseimage"], {stdio:"inherit"}).status)
      throw new Error("Failed to create base image");
   if(child_process.spawnSync("docker", ["rm", "base"], {stdio:"inherit"}).status)
      throw new Error("Failed to remove base container");

   // the correct approach is by far "--show-only=json-v1" and then pluck out .tests[].name; but that doesn't work on U18's cmake 3.10 since it lacks json-v1 output
   const test_query_result = child_process.spawnSync("docker", ["run", "--rm", "baseimage", "bash", "-e", "-o", "pipefail", "-c", `cd build; ctest -L '${tests_label}' --show-only | head -n -1 | cut -d ':' -f 2 -s | jq -cnR '[inputs | select(length>0)[1:]]'`]);
   if(test_query_result.status)
      throw new Error("Failed to discover tests with label")
   const tests = JSON.parse(test_query_result.stdout);

   let subprocesses = [];
   tests.forEach(t => {
      subprocesses.push(new Promise(resolve => {
         child_process.spawn("docker", ["run", "--security-opt", "seccomp=unconfined", "-e", "GITHUB_ACTIONS=True", "--name", t, "--init", "baseimage", "bash", "-c", `cd build; ctest --output-on-failure -R '^${t}$' --timeout ${test_timeout}`], {stdio:"inherit"}).on('close', code => resolve(code));
      }));
   });

   const results = await Promise.all(subprocesses);

   for(let i = 0; i < results.length; ++i) {
      if(results[i] === 0)
         continue;

      //failing test
      core.setFailed("Some tests failed");

      let extractor = tar.extract();
      let packer = tar.pack();

      extractor.on('entry', (header, stream, next) => {
         if(!header.name.startsWith(`__w/leap/leap/build`)) {
            stream.on('end', () => next());
            stream.resume();
            return;
         }

         header.name = header.name.substring(`__w/leap/leap/`.length);
         if(header.name !== "build/" && error_log_paths.filter(p => header.name.startsWith(p)).length === 0) {
            stream.on('end', () => next());
            stream.resume();
            return;
         }

         stream.pipe(packer.entry(header, next));
      }).on('finish', () => {packer.finalize()});

      child_process.spawn("docker", ["export", tests[i]]).stdout.pipe(extractor);
      stream.promises.pipeline(packer, zlib.createGzip(), fs.createWriteStream(`${log_tarball_prefix}-${tests[i]}-logs.tar.gz`));
   }
} catch(e) {
   core.setFailed(`Uncaught exception ${e.message}`);
}
