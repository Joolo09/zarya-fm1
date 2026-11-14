// SPDX-License-Identifier: GPL-3.0-only
import { App } from './app';
import { mount } from './ui/ui';

const app = new App();
mount(app, document.getElementById('app')!);
void app.start(new URL('./zarya.wasm', document.baseURI).href);
