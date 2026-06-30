#pragma once

#include "assets.h"

struct Scenery {
	SceneryTemplate scenery_template;
	Field root_fld;
	mu::Vec<StartInfo> start_infos;

	bool should_be_loaded;
};

inline Scenery scenery_new(SceneryTemplate& scenery_template) {
	return Scenery {
		.scenery_template = scenery_template,
		.should_be_loaded = true
	};
}

inline void scenery_load(Scenery& self) {
	self.root_fld = field_from_fld_file(self.scenery_template.fld);
	field_load_to_gpu(self.root_fld);

	self.start_infos = start_info_from_stp_file(self.scenery_template.stp);
	self.should_be_loaded = false;
}

inline void scenery_unload(Scenery& self) {
	field_unload_from_gpu(self.root_fld);
}
