"""Keep other products' claimed bytes reserved while rebuilding this product."""


def owned_sources(registry):
    return {name for name, entry in registry['sources'].items()
            if entry['party'] == 'first-party' and name.startswith('first-party/ultimate-ascend-')}
