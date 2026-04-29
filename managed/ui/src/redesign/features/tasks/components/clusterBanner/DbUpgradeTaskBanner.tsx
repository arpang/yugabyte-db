import { useState } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';

import { Task, TaskState, TaskType } from '../../dtos';
import { ClusterOperationBanner, ClusterOperationBannerType } from './ClusterOperationBanner';
import { precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { dbUpgradeMetadataQueryKey } from '@app/redesign/helpers/api';
import { yba } from '@yugabyte-ui-library/core';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { Link as MUILink } from '@material-ui/core';
import { DbUpgradeManagementSidePanel } from '@app/redesign/features/universe/universe-actions/software-upgrade/upgrade-management/DbUpgradeManagementSidePanel';
import { DbUpgradeRollBackModal } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeRollBackModal';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '@app/redesign/features/universe/universe-actions/software-upgrade/constants';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
const { YBButton } = yba;
interface DbUpgradeTaskBannerProps {
  task: Task;
  universeUuid: string;
}

export const DbUpgradeTaskBanner = ({ task, universeUuid }: DbUpgradeTaskBannerProps) => {
  const [isDbUpgradeManagementSidePanelOpen, setIsDbUpgradeManagementSidePanelOpen] =
    useState(false);
  const [isDbUpgradeRollBackModalOpen, setIsDbUpgradeRollBackModalOpen] = useState(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.clusterBanner'
  });
  const isDbUpgradeTask = task.type === TaskType.SOFTWARE_UPGRADE;
  const targetDbVersion = task.details?.versionNumbers?.ybSoftwareVersion ?? '';

  const dbUpgradeMetadataQuery = useQuery(
    dbUpgradeMetadataQueryKey.detail(universeUuid, {
      yb_software_version: targetDbVersion ?? ''
    }),
    () =>
      precheckSoftwareUpgrade(universeUuid, {
        yb_software_version: targetDbVersion ?? ''
      }),
    {
      enabled: !!targetDbVersion && isDbUpgradeTask
    }
  );

  if (!isDbUpgradeTask) {
    // DbUpgradeTaskBanner is only displayed for DB upgrade tasks
    return null;
  }

  const {
    ysql_major_version_upgrade: isYsqlMajorUpgrade = false,
    finalize_required: isFinalizationRequired = false
  } = dbUpgradeMetadataQuery.data ?? {};

  let bannerComponent = null;
  const openDbUpgradeManagementSidePanelButton = (
    <YBButton
      variant="secondary"
      size="medium"
      dataTestId="open-upgrade-monitor-button"
      onClick={() => setIsDbUpgradeManagementSidePanelOpen(true)}
    >
      {t('actions.openUpgradeMonitor')}
    </YBButton>
  );
  switch (task.status) {
    case TaskState.RUNNING:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.IN_PROGRESS}
          title={t('upgradingSoftware.title')}
          progressPercent={task.percentComplete ?? 0}
          actions={openDbUpgradeManagementSidePanelButton}
          description={
            <Trans
              t={t}
              i18nKey={
                isYsqlMajorUpgrade
                  ? 'upgradingSoftware.descriptionMajorDbUpgrade'
                  : 'upgradingSoftware.description'
              }
              components={{
                learnMoreLink: (
                  <MUILink
                    href={YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL}
                    target="_blank"
                    rel="noopener noreferrer"
                    underline="always"
                  />
                )
              }}
            />
          }
        />
      );
      break;
    case TaskState.PAUSED:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.PENDING_ACTION_YELLOW}
          title={t('upgradePausedForMonitoring.title')}
          progressPercent={task.percentComplete ?? 0}
          actions={openDbUpgradeManagementSidePanelButton}
          description={t('upgradePausedForMonitoring.description')}
        />
      );
      break;
    case TaskState.SUCCESS:
      if (isFinalizationRequired) {
        bannerComponent = (
          <ClusterOperationBanner
            type={ClusterOperationBannerType.PENDING_ACTION_YELLOW}
            title={t('finalizeOrRollBack.title')}
            actions={openDbUpgradeManagementSidePanelButton}
            description={t('finalizeOrRollBack.description')}
          />
        );
      } else {
        bannerComponent = (
          <ClusterOperationBanner
            type={ClusterOperationBannerType.SUCCESS}
            title={t('upgradeCompleted.title', {
              targetDbVersion: formatYbSoftwareVersionString(targetDbVersion ?? '')
            })}
            description={
              <Trans
                t={t}
                i18nKey="upgradeCompleted.description"
                components={{
                  rollBackLinkButton: (
                    <MUILink
                      onClick={() => setIsDbUpgradeRollBackModalOpen(true)}
                      underline="always"
                    />
                  )
                }}
              />
            }
          />
        );
      }
      break;
    case TaskState.FAILURE:
      bannerComponent = (
        <ClusterOperationBanner
          type={ClusterOperationBannerType.ERROR}
          title={t('softwareUpgradeFailed.title')}
          progressPercent={task.percentComplete ?? 0}
          actions={openDbUpgradeManagementSidePanelButton}
        />
      );
      break;
    case TaskState.CREATED:
    case TaskState.INITIALIZING:
    case TaskState.ABORTED:
    case TaskState.ABORT:
    case TaskState.UNKNOWN:
      bannerComponent = null;
      break;
    default:
      assertUnreachableCase(task.status);
  }
  return (
    <>
      {bannerComponent}
      <DbUpgradeManagementSidePanel
        modalProps={{
          open: isDbUpgradeManagementSidePanelOpen,
          onClose: () => setIsDbUpgradeManagementSidePanelOpen(false)
        }}
        universeUuid={universeUuid}
      />
      <DbUpgradeRollBackModal
        modalProps={{
          open: isDbUpgradeRollBackModalOpen,
          onClose: () => setIsDbUpgradeRollBackModalOpen(false)
        }}
        universeUuid={universeUuid}
      />
    </>
  );
};
