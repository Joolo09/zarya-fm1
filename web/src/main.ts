// SPDX-License-Identifier: GPL-3.0-only
import { App } from './app';
import { mount } from './ui/ui';
import wasmUrl from './assets/zarya.wasm?url';

const app = new App();
mount(app, document.getElementById('app')!);
void app.start(wasmUrl);
