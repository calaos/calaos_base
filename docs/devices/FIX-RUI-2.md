# FIX-RUI-2 — Widget climate « unsupported », device invisible (installer + calaos_base)

## Objectif
Recette du 15/10 : un widget Remote UI lié à un device climate s'affiche « Unsupported » sur le panneau ; le device n'apparaît ni dans la web app ni dans calaos-home.

## Livré
PR calaos/calaos_base#206, puis calaos/calaos_installer#75 (merger dans cet ordre).
- Cause 1 (installer) : `WidgetModel::widgetTypeForGuiType()` n'avait pas `climate` et écrivait le repli `Generic` (WidgetModel.cpp:328), que le firmware n'enregistre pas. `climate` → `Climate` (insensible à la casse), `Climate` proposé ; un widget déjà en `Generic` reprend le type de son IO à l'ouverture de l'éditeur si le panneau le dessine.
- Cause 2 (serveur) : un `<calaos:device>` sans `visible` n'avait pas de clé `visible` dans get_home ; web app et calaos_mobile n'affichent que `visible == "true"`. `Device` applique le défaut documenté par IODoc (`"true"`, un `false` explicite est gardé) ; les templates écrivent `visible="true"`. Firmware et RUI-1 hors de cause.
- Sur la box de l'utilisateur, les deux attributs ont été corrigés à la main le 15/10 en attendant ces correctifs.

## Mesuré
Tests rouges avant correctif (Device_test ×1, DeviceTemplates_test ×5, tst_remoteuiwidget ×8, tst_devicetemplates). make check 205/208 (python local seul), installer 20 suites / 623 cas, CI verte des deux côtés. Bout en bout : io.xml écrit par l'installer, vrai calaos_server (fa4319cb et master), flux du panneau rejoué dans le simulateur SDL 1.0.1-dev.13 : « Unsupported / Generic » avant, widget Climate après ; get_home `visible` absent avant, `"true"` après.

## Contre-mutation
CM0 : 0 rouge. Serveur : sans défaut / défaut false (ADeviceIsShown…, GetHomeCarriesTheDeviceObject, GetHomeCarriesTheStatusFields), écrase le fichier (ADeviceHiddenByItsFileStaysHidden), template sans visible / à false (EveryTemplateShowsItsDevice). Installer : sans `climate` (8 rouges), sensible à la casse (theFamilyIsMatchedWhateverItsCase), jamais retypé, retypé vers un type non dessiné, tout type retypé, Climate non proposé (aWidgetSavedAsGenericIsRetypedWhenTheEditorOpens).

## Reste ouvert
Les mappings Gauge/Text/Timer de l'installer restent sans widget firmware.
