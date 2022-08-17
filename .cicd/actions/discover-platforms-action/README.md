## Discover Platform Image Names & Missing Images

This action discovers the appropriate container registry image tags for a collection of "platforms" (Dockerfiles) defined in the current repo. The returned values are based on the owner of the repo, a configured package name, and the sha256 of the Dockerfile. It will also return an array of platform names whose images are not present in the container registry so the workflow can then go on to build and push them (this action will _not_ build and push them, as most likely you'd want to parallelize them in a follow on matrix'ed job).

### Inputs

Three inputs are required:
* **platform-file** - Path to file in repository that defines platforms & dockerfiles
* **package-name** - The package name in this repo owner's container registry to look for existing container images
* **password** - Token that has read access to repository & package in container registry

Example:
```
    steps:
      - name: Discover Platforms
        id: discover
        uses: AntelopeIO/discover-platforms-action@v1
        with:
          platform-file: .cicd/platforms.json
          password: ${{secrets.GITHUB_TOKEN}}
          package-name: builder
```
Where `.cicd/platforms.json` is, for example
```json
{
   "ubuntu18": {
      "dockerfile": ".cicd/platforms/ubuntu18.Dockerfile"
   },
   "ubuntu20": {
      "dockerfile": ".cicd/platforms/ubuntu20.Dockerfile"
   }
}
```

### Outputs
* **platforms** - The object from the `platform-file` input (see above) but with the container image and tag added
* **missing-platforms** - An array of platforms that were not found in the container registry (could be an empty array)

Example for `platforms`:
```json
{
  ubuntu18: {
    dockerfile: '.cicd/platforms/ubuntu18.Dockerfile',
    image: 'ghcr.io/AntelopeIO/builder:8c724d9fe6f9819d9f0bd6bdff592dd971d17bd8b69fa7fef05897b8ba7e0291'
  },
  ubuntu20: {
    dockerfile: '.cicd.platforms/ubuntu20.Dockerfile',
    image: 'ghcr.io/AntelopeIO/builder:130e29695cbbd21fe2ad5c5ba6e320888a97417ddc7baa0a7a6f7b3cd145cbd2'
  }
}
```

Example `missing-platforms`:
```json
["ubuntu18", "ubuntu20"]
```
or if both tags in the above example had already existed,
```json
[]
```

### Rebuilding `dist`
```
ncc build main.mjs --license licenses.txt
```

